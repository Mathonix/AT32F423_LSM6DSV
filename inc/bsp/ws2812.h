#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One WS2812B-4020: PA8 -> DIN, byte order on wire is GRB. */
void ws2812_init(void);
void ws2812_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
typedef enum
{
  WS2812_MODE_6AXIS = 0,
  WS2812_MODE_9AXIS = 1
} ws2812_mode_t;

void ws2812_normal_task(uint32_t now_ms, ws2812_mode_t mode, uint8_t calibration_failed, uint8_t mag_rejected, uint8_t settings_pending_reboot, uint8_t history_error);
void ws2812_settings_task(uint32_t now_ms, ws2812_mode_t mode);
void ws2812_calibration_task(uint32_t now_ms);
void ws2812_acc_calibration_task(uint32_t now_ms, uint8_t face, uint8_t active, uint8_t failed);
void ws2812_show_error(uint8_t code);

#ifdef __cplusplus
}
#endif
#endif
