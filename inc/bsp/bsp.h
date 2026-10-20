/**
 * Board support for AT32F423KCU7-4:
 *   LED_STA   PB8
 *   UART_TX   PA0  USART4_TX  MUX8
 *   UART_RX   PA1  USART4_RX  MUX8
 *   LSM_INT1  PB0
 *   LSM_INT2  PB1
 */

#ifndef __BSP_H
#define __BSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423.h"
#include "app_config.h"
#include <stdio.h>

#define LED_GPIO                 GPIOB
#define LED_PIN                  GPIO_PINS_8

#define PRINT_UART               USART4
#define PRINT_UART_BAUDRATE      APP_UART_BAUD

void bsp_init(void);
void bsp_systick_tick(void);
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
uint32_t dwt_cycles(void);
/* Foreground only. Extends CYCCNT to a 64-bit microsecond clock. The 8 kHz
 * scheduler uses micros(), whose low 32 bits match this clock. */
uint64_t dwt_time_us(void);
uint32_t micros(void);
uint32_t millis(void);
void led_on(void);
void led_off(void);
void led_toggle(void);
uint8_t lsm_int1_read(void);
uint8_t lsm_int2_read(void);
int uart_dma_busy(void);
/* Control frames are copied into persistent storage before DMA starts. */
int uart_control_enqueue(const uint8_t *data, uint16_t len);
void uart_tx_task(void);
int uart_tx_idle(void);
int uart_dma_send(const uint8_t *data, uint16_t len);
#if APP_UART_TX_PAD_TEST
/* Diagnostic CUSTOM framing only; padding is not a telemetry frame/sample. */
int uart_tx_padding_set(unsigned bytes);
#endif
int uart_read_byte(uint8_t *ch);
void uart_rx_isr(void);
extern volatile uint32_t uart_rx_overruns;
extern volatile uint32_t uart_rx_drops;
extern volatile uint32_t uart_rx_dma_overwrites;
extern volatile uint32_t uart_rx_dma_errors;

#ifdef __cplusplus
}
#endif

#endif
