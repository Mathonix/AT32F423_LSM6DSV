#ifndef BL_IO_H
#define BL_IO_H
#include <stdint.h>
void bl_io_init(void);
void bl_io_deinit(void);
void bl_io_task(void);
int bl_io_read(uint8_t *b);
int bl_io_write(const uint8_t *p, uint16_t n);
int bl_usb_ready(void);
#endif
