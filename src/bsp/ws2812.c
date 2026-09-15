#include "ws2812.h"
#include "bsp.h"
#include "app_config.h"

#define WS_GPIO       GPIOA
#define WS_PIN        GPIO_PINS_8
#define WS_PERIOD_MS  20U
#define WS_MIN_LEVEL 1U
#define WS_MAX_LEVEL  45U
#define WS_PHASE_STEPS 150U /* 150 x 20 ms = 3.0 s */
#define WS_HALF_STEPS  75U

static uint32_t ws_last_ms;
static uint8_t ws_phase;

static inline void wait_until(uint32_t deadline)
{
  while((int32_t)(DWT->CYCCNT - deadline) < 0)
  {
  }
}

static void send_byte(uint8_t value, uint32_t t0h, uint32_t t1h, uint32_t period)
{
  uint32_t bit;
  for(bit = 0U; bit < 8U; bit++)
  {
    uint32_t start;
    uint32_t high;
    WS_GPIO->scr = WS_PIN;
    start = DWT->CYCCNT;
    high = (value & 0x80U) ? t1h : t0h;
    wait_until(start + high);
    WS_GPIO->clr = WS_PIN;
    wait_until(start + period);
    value <<= 1;
  }
}

void ws2812_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
  /* Use deliberately well-separated pulse widths. This gives substantially
   * more decoding margin than placing both symbols near their nominal limits:
   * T0H=0.30 us, T1H=0.80 us, bit period=1.25 us. */
  uint32_t mhz = system_core_clock / 1000000U;
  uint32_t t0h = (mhz * 30U) / 100U;
  uint32_t t1h = (mhz * 80U) / 100U;
  uint32_t period = (mhz * 125U) / 100U;
  uint32_t primask;

  if(t0h < 1U) t0h = 1U;
  if(t1h <= t0h) t1h = t0h + 1U;
  if(period <= t1h) period = t1h + 1U;

  primask = __get_PRIMASK();
  __disable_irq();
  send_byte(green, t0h, t1h, period);
  send_byte(red, t0h, t1h, period);
  send_byte(blue, t0h, t1h, period);
  WS_GPIO->clr = WS_PIN;
  if(primask == 0U)
  {
    __enable_irq();
  }
}

void ws2812_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = WS_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_DOWN;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(WS_GPIO, &gpio_init_struct);
  WS_GPIO->clr = WS_PIN;
  delay_us(100U);

  /* Blue means firmware startup / stationary calibration. */
  ws2812_set_rgb(0U, 0U, 12U);
  ws_last_ms = millis();
  ws_phase = 0U;
}

static uint8_t breathing_level(uint8_t phase)
{
  uint32_t half;
  uint32_t x;
  uint32_t x2;
  uint32_t eased;
  uint32_t perceived;

  /* phase 0..149: 0..75 fades up, 76..149 fades down. Smoothstep makes
   * the slope zero at both ends. Keep the direct smoothstep result here;
   * a second gamma square leaves too few distinct levels in the 10..50 range. */
  half = (phase <= WS_HALF_STEPS) ? phase : (WS_PHASE_STEPS - phase);
  x = (half * 32768U + (WS_HALF_STEPS / 2U)) / WS_HALF_STEPS;                /* Q15, 0..1 */
  x2 = (uint32_t)(((uint64_t)x * x) >> 15);
  eased = (uint32_t)(((uint64_t)x2 * (98304U - 2U * x)) >> 15);
  perceived = eased;
  return (uint8_t)(WS_MIN_LEVEL +
                   (((WS_MAX_LEVEL - WS_MIN_LEVEL) * perceived + 16384U) >> 15));
}

void ws2812_normal_task(uint32_t now_ms, ws2812_mode_t mode, uint8_t calibration_failed, uint8_t mag_rejected, uint8_t settings_pending_reboot, uint8_t history_error)
{
  uint8_t level;
  uint8_t red = 0U;
  uint8_t green = 0U;
  uint8_t blue = 0U;

  if((uint32_t)(now_ms - ws_last_ms) < WS_PERIOD_MS)
  {
    return;
  }
  /* Avoid accumulated timing drift while also recovering cleanly if the
   * main loop was delayed by a debug halt. */
  if((uint32_t)(now_ms - ws_last_ms) >= (WS_PERIOD_MS * 3U))
  {
    ws_last_ms = now_ms;
  }
  else
  {
    ws_last_ms += WS_PERIOD_MS;
  }

  level = breathing_level(ws_phase);
  /* Status colors: 6-axis=green, 9-axis=cyan. */
  switch(mode)
  {
    case WS2812_MODE_9AXIS:
      green = level;
      blue = level;
      break;
    case WS2812_MODE_6AXIS:
    default:
      green = level;
      break;
  }
  /* A rejected magnetic update alternates between the normal mode color and
   * green. Keep the normal color visible longer, so the warning is clear
   * without making the unit look permanently like a 6-axis device. */
  if((mag_rejected != 0U) && (mode != WS2812_MODE_6AXIS))
  {
    const uint32_t base_ms = APP_WS2812_MAG_REJECT_BASE_MS;
    const uint32_t green_ms = APP_WS2812_MAG_REJECT_GREEN_MS;
    const uint32_t cycle_ms = base_ms + green_ms;
    const uint32_t phase_ms = (cycle_ms != 0U) ? (now_ms % cycle_ms) : 0U;
    if(phase_ms >= base_ms)
    {
      red = 0U;
      green = level;
      blue = 0U;
    }
  }

  /* A fallback warning briefly overlays the normal color once per second.
   * It is intentionally short so the breathing state remains visible. */
  if((calibration_failed != 0U) && ((now_ms % 1000U) < 140U))
  {
    ws2812_set_rgb(48U, 0U, 0U); /* failed startup calibration: red flash */
  }
  else if(((settings_pending_reboot != 0U) || (history_error != 0U)) &&
          ((now_ms % 1000U) < 120U))
  {
    ws2812_set_rgb(48U, 32U, 0U); /* yellow pending/error notification */
  }
  else
  {
    ws2812_set_rgb(red, green, blue);
  }
  ws_phase++;
  if(ws_phase >= WS_PHASE_STEPS)
  {
    ws_phase = 0U;
  }
}
void ws2812_show_error(uint8_t code)
{
  /* Red is fatal IMU/VQF startup failure; amber identifies mag failure. */
  if(code == 2U)
  {
    ws2812_set_rgb(48U, 18U, 0U);
  }
  else
  {
    ws2812_set_rgb(48U, 0U, 0U);
  }
}




void ws2812_settings_task(uint32_t now_ms, ws2812_mode_t mode)
{
  if((uint32_t)(now_ms - ws_last_ms) < WS_PERIOD_MS) return;
  ws_last_ms = now_ms;
  if(mode == WS2812_MODE_9AXIS)
    ws2812_set_rgb(0U, 32U, 32U);
  else
    ws2812_set_rgb(0U, 32U, 0U);
}

void ws2812_calibration_task(uint32_t now_ms)
{
  if((uint32_t)(now_ms - ws_last_ms) < WS_PERIOD_MS) return;
  ws_last_ms = now_ms;
  if((now_ms % 1000U) < 180U)
    ws2812_set_rgb(0U, 0U, 18U);
  else
    ws2812_set_rgb(0U, 0U, 0U);
}

void ws2812_acc_calibration_task(uint32_t now_ms, uint8_t face, uint8_t active, uint8_t failed)
{
  uint8_t on = 0U;
  uint8_t i;
  static uint32_t last_ms;
  if(!active && !failed) return;
  if((uint32_t)(now_ms-last_ms) < WS_PERIOD_MS) return;
  last_ms=now_ms;
  if(failed) { ws2812_set_rgb(((now_ms % 1000U) < 180U) ? 48U : 0U, 0U, 0U); return; }
  /* Yellow prompt: repeat N short flashes for face N, then a pause. */
  if(face == 0U || face > 6U) face = 1U;
  {
    uint32_t phase = now_ms % 1600U;
    for(i=0U; i<face; ++i) if(phase >= (uint32_t)i*180U && phase < (uint32_t)i*180U+110U) on=1U;
  }
  ws2812_set_rgb(on ? 42U : 0U, on ? 28U : 0U, 0U);
}
