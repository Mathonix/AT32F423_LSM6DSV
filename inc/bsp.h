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
#include <stdio.h>

#define LED_GPIO                 GPIOB
#define LED_PIN                  GPIO_PINS_8

#define PRINT_UART               USART4
#define PRINT_UART_BAUDRATE      2000000U

void bsp_init(void);
void bsp_systick_tick(void);
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
uint32_t dwt_cycles(void);
uint32_t millis(void);
void led_on(void);
void led_off(void);
void led_toggle(void);
uint8_t lsm_int1_read(void);
uint8_t lsm_int2_read(void);
int uart_dma_busy(void);
int uart_dma_send(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
