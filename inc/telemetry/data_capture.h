#ifndef DATA_CAPTURE_H
#define DATA_CAPTURE_H
#include "protocol.h"
typedef struct {
  struct { uint32_t token, seq, deadline; uint16_t hz, divider; } ports[3];
} data_capture_t;
int data_capture_rate_valid(uint16_t hz);
uint8_t data_capture_configure(data_capture_t *s,unsigned port,const uint8_t *p,unsigned len,uint32_t now,uint16_t fusion_hz);
void data_capture_stop(data_capture_t *s);
int data_capture_active(const data_capture_t *s);
int data_capture_due(data_capture_t *s,unsigned port,uint32_t now,uint32_t fusion_n);
/* Reserve bandwidth for capture; ordinary telemetry on that port is <=100Hz.
 * Commands, other ports and saved output settings are unaffected. */
int data_capture_telemetry_due(const data_capture_t *s,unsigned port,uint32_t fusion_n,uint16_t fusion_hz);
int data_capture_command_mutates(uint8_t cmd);
uint16_t data_capture_pack(data_capture_t *s,unsigned port,uint8_t *frame,uint16_t capacity,uint32_t sample_us,uint8_t flags,const float values[9]);
#endif
