#ifndef SPI_WAIT_H
#define SPI_WAIT_H
#include <stdint.h>
/* Read hardware status every iteration without decrementing zero. */
static inline int spi_wait_flags(volatile uint32_t *status, uint32_t mask,
                                 uint32_t expected, uint32_t remaining)
{
  while((*status & mask) != expected) {
    if(!remaining) return -1;
    --remaining;
  }
  return 0;
}
#endif
