#ifndef FUSION_ALGORITHM_H
#define FUSION_ALGORITHM_H
#include <stdint.h>
enum { FUSION_ALGORITHM_VQF=0, FUSION_ALGORITHM_EKF=1, FUSION_ALGORITHM_COUNT=2 };
/* Report-only identity of the compile-time 8 kHz experiment. Never stored in
 * the legacy settings field or selectable by a normal VQF/MEKF image. */
#define FUSION_ALGORITHM_ESKF8K 2U
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
