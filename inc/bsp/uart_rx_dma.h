#ifndef UART_RX_DMA_H
#define UART_RX_DMA_H

#include <stdint.h>

/* Circular DMA index math for a power-of-two buffer.
 *
 * pos is the next write index: (size - NDTR) mod size. A running AT32
 * circular channel reloads NDTR to size, which is the same position as the
 * initial NDTR and is reported as index 0. NDTR 0 is treated as that reload.
 *
 * One slot stays empty, so the buffer holds at most size-1 bytes. The modular
 * distance is exact only when fewer than size bytes arrive between observes.
 * A silent gap of exactly size bytes returns to the same index and is invisible.
 */

typedef struct {
  uint32_t overwrites;
  uint16_t read;
  uint16_t last_pos;
  uint16_t available;
} uart_rx_dma_obs_t;

static inline uint16_t uart_rx_dma_pos(uint16_t size, uint16_t ndtr)
{
  if(ndtr == 0U || ndtr > size) ndtr = size;
  return (uint16_t)((size - ndtr) & (uint16_t)(size - 1U));
}

static inline uart_rx_dma_obs_t uart_rx_dma_observe(uint16_t size, uint16_t read,
                                                   uint16_t last_pos, uint16_t pos)
{
  uint16_t mask = (uint16_t)(size - 1U);
  uint16_t arrived = (uint16_t)((pos - last_pos) & mask);
  uint16_t buffered = (uint16_t)((last_pos - read) & mask);
  uint16_t room = (uint16_t)((size - 1U) - buffered);
  uart_rx_dma_obs_t obs;
  obs.overwrites = 0U;
  obs.last_pos = pos;
  if(arrived > room) {
    obs.overwrites = (uint32_t)arrived - (uint32_t)room;
    read = (uint16_t)((pos - (uint16_t)(size - 1U)) & mask);
  }
  obs.read = read;
  obs.available = (uint16_t)((pos - read) & mask);
  return obs;
}

#endif
