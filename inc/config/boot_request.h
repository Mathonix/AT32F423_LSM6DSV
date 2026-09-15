#ifndef BOOT_REQUEST_H
#define BOOT_REQUEST_H

/* Keep this mailbox outside the startup stack area and outside both images'
 * BSS. The application writes the magic immediately before reset; the
 * bootloader reads and clears it before deciding whether to stay in update
 * mode. */
#define APP_BOOT_REQUEST_ADDR  0x20008000U
#define APP_BOOT_REQUEST_MAGIC 0x424F4F54U

#endif
