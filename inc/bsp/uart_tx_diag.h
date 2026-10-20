#ifndef UART_TX_DIAG_H
#define UART_TX_DIAG_H
#include <stdint.h>
#ifndef APP_UART_TX_DIAG
#define APP_UART_TX_DIAG 0
#endif
/* Main-context ledger only. TC times are polling observations.
 * Flags bit21: DMA_START/TC count byte blocks; TELEMETRY/CONTROL_DMA_TC count
 * completed logical frames in those blocks, so their sum differs from TC.
 * Queue counts/high-water occupy bits8..11/12..15; bit22 means saturation.
 * No source frame is synthesized by the byte batching transport. */
enum {
 UT_MAGIC,UT_TOKEN,UT_PACK_OK,UT_PACK_FAIL,UT_PACK_BYTES,
 UT_OFFER,UT_ACCEPT,UT_BUSY_REJECT,UT_CONTROL_YIELD,
 UT_CTRL_ENQUEUE,UT_CTRL_REJECT,UT_CTRL_HIGH,UT_CTRL_COUNT,
 UT_DMA_START,UT_DMA_TC,UT_DMA_ERROR,UT_WIRE_IDLE,UT_WIRE_CHAINED,
 UT_ACTIVE_KIND,UT_ACTIVE_LEN,UT_START_CY,UT_DMA_MAX_CY,UT_WIRE_MAX_CY,
 UT_CTRL_OLDEST_CY,UT_CTRL_MAX_AGE_CY,UT_POSE_SEQ,UT_POSE_US,
 UT_DMA_LAST_SEQ,UT_DMA_TC_SEQ,UT_WIRE_LAST_SEQ,UT_FLAGS,UT_WIRE_LAST_CY,
 UT_CTRL_FULL_MAX_CY,UT_CTRL_FULL_START_CY,UT_DMA_BYTES,
 UT_TELEMETRY_DMA_TC,UT_CONTROL_DMA_TC,UT_WIRE_BURST_BYTES,
 UT_LAST_PACK_LEN,UT_FORMAT_MASK,UT_WORDS
};
#if APP_UART_TX_DIAG
extern volatile uint32_t uart_tx_diag[UT_WORDS];
extern uint32_t uart_tx_diag_snapshot[UT_WORDS];
void uart_tx_diag_pack(uint16_t len,uint32_t seq,uint32_t us,uint8_t format,uint16_t mask);
uint32_t uart_tx_diag_freeze(void);
#endif
#endif
