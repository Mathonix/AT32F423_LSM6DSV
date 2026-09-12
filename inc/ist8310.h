#ifndef IST8310_H
#define IST8310_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int ist8310_init(void);
int ist8310_read(int16_t mag[3]);
int ist8310_start_measurement(void);
int ist8310_data_ready(void);
int ist8310_read_ready(int16_t mag[3]);
uint8_t ist8310_get_addr(void);
#ifdef __cplusplus
}
#endif
#endif
