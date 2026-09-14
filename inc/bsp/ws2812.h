#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One WS2812B-4020: PA8 -> DIN, byte order on wire is GRB. */
void ws2812_init(void);
void ws2812_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
void ws2812_normal_task(uint32_t now_ms);
void ws2812_show_error(uint8_t code);

#ifdef __cplusplus
}
#endif
#endif
