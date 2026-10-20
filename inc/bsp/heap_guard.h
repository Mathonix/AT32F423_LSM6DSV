#ifndef HEAP_GUARD_H
#define HEAP_GUARD_H
#include <stdint.h>
/* magic, break, peak bytes, successes, failures, last errno, ISR rejects,
 * configured capacity. Endpoint and high-water diagnostics, never allocated. */
extern volatile uint32_t heap_guard_live[8];
void heap_guard_init(void);
#endif
