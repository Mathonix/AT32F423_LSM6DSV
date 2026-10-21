#ifndef FUSION_ALGORITHM_H
#define FUSION_ALGORITHM_H
#include <stdint.h>
/* VQF is the shipped engine, EKF is the legacy alternative, ESKF is the 8 kHz
 * single-QR engine. Each engine keeps its own rest/bias accounting; the app
 * level enable flags are mirrored into whichever engine is selected. */
enum { FUSION_ALGORITHM_VQF=0, FUSION_ALGORITHM_EKF=1, FUSION_ALGORITHM_ESKF=2,
       FUSION_ALGORITHM_COUNT=3 };
#ifdef __cplusplus
extern "C" {
#endif
/* Select before vqf_init; the host saves selection and performs a normal reset. */
void fusion_select_algorithm(uint8_t algorithm);
uint8_t fusion_get_algorithm(void);
#ifdef __cplusplus
}
#endif
#endif
