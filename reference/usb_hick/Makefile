ROOT := E:/Desktop/CV_resume/Program/AT32F423_Firmware_Library-master/AT32F423_Firmware_Library-master
OUT := official_usb_hick/build
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE := arm-none-eabi-size
CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DAT32F423KCU7_4 -DUSE_STDPERIPH_DRIVER -DAT_START_F423_V1 -DUSB_VIRTUAL_COMPORT -O1 -g -Wall -ffunction-sections -fdata-sections -fno-common -MMD -MP
INCS := -I$(ROOT)/libraries/cmsis/cm4/core_support -I$(ROOT)/libraries/cmsis/cm4/device_support -I$(ROOT)/libraries/drivers/inc -I$(ROOT)/project/at32f423_board -I$(ROOT)/project/at_start_f423/examples/usb_device/virtual_comport/inc -I$(ROOT)/middlewares/usb_drivers/inc -I$(ROOT)/middlewares/usbd_class/cdc
LDS := $(ROOT)/libraries/cmsis/cm4/device_support/startup/gcc/linker/AT32F423xC_FLASH.ld
SRCS := \
 $(ROOT)/project/at_start_f423/examples/usb_device/virtual_comport/src/at32f423_clock.c \
 $(ROOT)/project/at_start_f423/examples/usb_device/virtual_comport/src/at32f423_int.c \
 $(ROOT)/project/at_start_f423/examples/usb_device/virtual_comport/src/main.c \
 $(ROOT)/project/at32f423_board/at32f423_board.c \
 $(ROOT)/libraries/drivers/src/at32f423_acc.c \
 $(ROOT)/libraries/drivers/src/at32f423_crm.c \
 $(ROOT)/libraries/drivers/src/at32f423_exint.c \
 $(ROOT)/libraries/drivers/src/at32f423_gpio.c \
 $(ROOT)/libraries/drivers/src/at32f423_misc.c \
 $(ROOT)/libraries/drivers/src/at32f423_pwc.c \
 $(ROOT)/libraries/drivers/src/at32f423_scfg.c \
 $(ROOT)/libraries/drivers/src/at32f423_usart.c \
 $(ROOT)/libraries/drivers/src/at32f423_usb.c \
 $(ROOT)/libraries/cmsis/cm4/device_support/system_at32f423.c \
 $(ROOT)/middlewares/usb_drivers/src/usb_core.c \
 $(ROOT)/middlewares/usb_drivers/src/usbd_core.c \
 $(ROOT)/middlewares/usb_drivers/src/usbd_int.c \
 $(ROOT)/middlewares/usb_drivers/src/usbd_sdr.c \
 $(ROOT)/middlewares/usbd_class/cdc/cdc_class.c \
 $(ROOT)/middlewares/usbd_class/cdc/cdc_desc.c
OBJS := $(patsubst %.c,$(OUT)/%.o,$(notdir $(SRCS)))
all: $(OUT)/official_usb_hick.elf $(OUT)/official_usb_hick.hex $(OUT)/official_usb_hick.bin
$(OUT):
	mkdir -p $(OUT)
$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@
$(OUT)/startup_at32f423.o: $(ROOT)/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s | $(OUT)
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@
$(OUT)/official_usb_hick.elf: $(OBJS) $(OUT)/startup_at32f423.o
	$(CC) -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -T$(LDS) -Wl,--gc-sections -Wl,-Map=$(OUT)/official_usb_hick.map --specs=nano.specs --specs=nosys.specs $(OBJS) $(OUT)/startup_at32f423.o -lm -o $@
	$(SIZE) $@
$(OUT)/official_usb_hick.hex: $(OUT)/official_usb_hick.elf
	$(OBJCOPY) -O ihex $< $@
$(OUT)/official_usb_hick.bin: $(OUT)/official_usb_hick.elf
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(OUT)
