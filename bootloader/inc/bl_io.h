#ifndef BL_IO_H
#define BL_IO_H
#include <stdint.h>
typedef enum { BL_IO_UART = 0, BL_IO_USB = 1, BL_IO_PORT_COUNT = 2 } bl_io_port_t;
void bl_io_init(void);
void bl_io_deinit(void);
void bl_io_task(void);
int bl_io_read(bl_io_port_t *source, uint8_t *b);
int bl_io_write_to(bl_io_port_t destination, const uint8_t *p, uint16_t n);
int bl_usb_ready(void);
#endif
