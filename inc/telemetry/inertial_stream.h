#ifndef INERTIAL_STREAM_H
#define INERTIAL_STREAM_H
#include "protocol.h"
typedef struct {
  struct { uint32_t token, seq, deadline; uint16_t divider, count; float world_sum[3], gyro_sum[3]; } ports[3];
  uint32_t cycles, us, remainder;
  uint8_t clock_ready;
} inertial_stream_t;
/* Returns protocol ACK status. Lease renewals keep sample sequence unchanged. */
uint8_t inertial_stream_configure(inertial_stream_t *s, unsigned port, const uint8_t *p,
                                 unsigned len, uint32_t now_ms, uint16_t fusion_hz);
uint32_t inertial_stream_clock(inertial_stream_t *s, uint32_t cycles, uint32_t clock_hz);
void inertial_stream_stop(inertial_stream_t *s);
int inertial_stream_active(const inertial_stream_t *s);
/* Average every fusion sample in earth coordinates before decimation. */
void inertial_stream_accumulate(inertial_stream_t *s, const float q[4], const float acc[3], const float gyro[3]);
int inertial_stream_due(inertial_stream_t *s, unsigned port, uint32_t now_ms);
uint16_t inertial_stream_pack(inertial_stream_t *s, unsigned port, uint8_t *frame,
                             uint16_t capacity, const float q[4], uint8_t flags, uint16_t fusion_hz);
#endif
