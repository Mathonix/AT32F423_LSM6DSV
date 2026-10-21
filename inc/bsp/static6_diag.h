#ifndef STATIC6_DIAG_H
#define STATIC6_DIAG_H
#include "app_config.h"
#include <stdint.h>
#if APP_STATIC6_DIAG
#ifdef __cplusplus
extern "C" {
#endif
enum { S6_PRED6,S6_PRED9,S6_ACC_CALL,S6_BROAD_REJECT,S6_REST,
       S6_DECIM_WAIT,S6_PLAN,S6_NORM_REJECT,S6_ATTEMPT6,S6_OK6,
       S6_NUMERIC_REJECT6,S6_ANGLE_REJECT6,S6_OK9,S6_REJECT9,S6_COUNTS };
/* Exact integer RAW moments; float Welford corrected-rate moments.
 * A block is 8000 consumed frames, not an assumed exact wall-clock second.
 * Counts are cumulative u32. Published block is immutable under seq lock. */
typedef struct {
  uint32_t window,n,first_cy,last_cy;
  /* abs(sum)<=8000*32768=262144000 fits signed32 exactly. Squares need64. */
  int32_t sum[7]; uint64_t square[7];
  int16_t lo[7],hi[7]; /* gx gy gz ax ay az temperature: register LSB */
  float mean[3],m2[3],minimum[3],maximum[3]; /* actual integrate rate rad/s */
  uint32_t counts[S6_COUNTS];
  float acc_length,gyro_norm,innovation[3],bias[3]; /* last attempted six gravity update */
} static6_block_t;
extern static6_block_t static6_work;
extern volatile static6_block_t static6_published;
extern volatile uint32_t static6_seq,static6_enabled;
void static6_raw(const int16_t gyro[3],const int16_t acc[3],int16_t temp,uint32_t cy);
void static6_rate(const float corrected[3]);
void static6_finish(void);
#ifdef __cplusplus
}
#endif
#endif
#endif
