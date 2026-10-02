#ifndef TEST_CAN_CONF_H
#define TEST_CAN_CONF_H
#include <stdint.h>
#define __DMB() ((void)0)
#define TRUE 1
#define FALSE 0
#define SUCCESS 1
#define RESET 0
#define CAN2 2
#define GPIOA 0
#define CRM_GPIOA_PERIPH_CLOCK 0
#define CRM_CAN2_PERIPH_CLOCK 1
#define GPIO_DRIVE_STRENGTH_STRONGER 0
#define GPIO_OUTPUT_PUSH_PULL 0
#define GPIO_MODE_MUX 0
#define GPIO_PINS_2 4
#define GPIO_PINS_3 8
#define GPIO_PULL_NONE 0
#define GPIO_PINS_SOURCE2 2
#define GPIO_PINS_SOURCE3 3
#define GPIO_MUX_9 9
#define CAN_MODE_COMMUNICATE 0
#define CAN_DISCARDING_FIRST_RECEIVED 0
#define CAN_SENDING_BY_ID 0
#define CAN_RSAW_2TQ 1
#define CAN_BTS1_10TQ 9
#define CAN_BTS1_12TQ 11
#define CAN_BTS2_4TQ 3
#define CAN_FILTER_MODE_ID_MASK 0
#define CAN_FILTER_FIFO0 0
#define CAN_FILTER_32BIT 0
#define CAN_RX_FIFO0 0
#define CAN_ID_STANDARD 0
#define CAN_ID_EXTENDED 1
#define CAN_TFT_DATA 0
#define CAN_TFT_REMOTE 1
#define CAN_TX_MAILBOX2 2
#define CAN_BOF_FLAG 1
#define CAN_EPF_FLAG 2
#define CAN_RF0OF_FLAG 3
#define CAN_TM0TCF_FLAG 10
#define CAN_TX_STATUS_SUCCESSFUL 1
#define CAN_TX_STATUS_FAILED 2
#define CAN_TX_STATUS_PENDING 3
#define CAN_TX_STATUS_NO_EMPTY 4
typedef int can_transmit_status_type;
typedef int can_tx_mailbox_num_type;
typedef struct { int gpio_drive_strength, gpio_out_type, gpio_mode, gpio_pins, gpio_pull; } gpio_init_type;
typedef struct { int mode_selection, ttc_enable, aebo_enable, aed_enable, prsf_enable, mdrsel_selection, mmssr_selection; } can_base_type;
typedef struct { int baudrate_div, rsaw_size, bts1_size, bts2_size; } can_baudrate_type;
typedef struct { int filter_activate_enable, filter_mode, filter_fifo, filter_number, filter_bit, filter_id_high, filter_id_low, filter_mask_high, filter_mask_low; } can_filter_init_type;
typedef struct { uint32_t standard_id, extended_id; int id_type, frame_type; uint8_t dlc, data[8]; } can_tx_message_type;
typedef can_tx_message_type can_rx_message_type;
#define crm_periph_clock_enable(...) ((void)0)
#define gpio_default_para_init(...) ((void)0)
#define gpio_init(...) ((void)0)
#define gpio_pin_mux_config(...) ((void)0)
#define can_reset(...) ((void)0)
#define can_default_para_init(...) ((void)0)
#define can_base_init(...) SUCCESS
#define can_baudrate_default_para_init(...) ((void)0)
#define can_filter_default_para_init(...) ((void)0)
#define can_filter_init(...) ((void)0)
#define can_transmit_error_counter_get(...) 0
#define can_receive_error_counter_get(...) 0
#define can_error_type_record_get(...) 0
int can_baudrate_set(int bus, const can_baudrate_type *b);
int can_flag_get(int bus, int flag);
void can_flag_clear(int bus, int flag);
void can_transmit_cancel(int bus, int mailbox);
int can_receive_message_pending_get(int bus, int fifo);
void can_message_receive(int bus, int fifo, can_rx_message_type *message);
uint8_t can_message_transmit(int bus, const can_tx_message_type *message);
int can_transmit_status_get(int bus, int mailbox);
#endif
