TARGET:=at32f423_bootloader
LIB:=../AT32F423_Firmware_Library
BUILD:=build
CC:=arm-none-eabi-gcc
OBJCOPY:=arm-none-eabi-objcopy
CHIP:=AT32F423KCU7_4
CFLAGS:=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -D$(CHIP) -DUSE_STDPERIPH_DRIVER -DVECT_TAB_OFFSET=0x0000 -O1 -g -Wall -ffunction-sections -fdata-sections -MMD -MP
INC:=-Iinc -I../inc/config -I$(LIB)/libraries/cmsis/cm4/core_support -I$(LIB)/libraries/cmsis/cm4/device_support -I$(LIB)/libraries/drivers/inc -I../middleware/usb_drivers/inc -I../middleware/usbd_class/cdc
SRCS:=src/main.c src/bl_io.c src/bl_protocol.c src/usb_delay.c src/bl_syscalls.c $(LIB)/libraries/cmsis/cm4/device_support/system_at32f423.c $(LIB)/libraries/drivers/src/at32f423_crm.c $(LIB)/libraries/drivers/src/at32f423_gpio.c $(LIB)/libraries/drivers/src/at32f423_misc.c $(LIB)/libraries/drivers/src/at32f423_usart.c $(LIB)/libraries/drivers/src/at32f423_flash.c $(LIB)/libraries/drivers/src/at32f423_pwc.c $(LIB)/libraries/drivers/src/at32f423_acc.c $(LIB)/libraries/drivers/src/at32f423_usb.c ../middleware/usb_drivers/src/usb_core.c ../middleware/usb_drivers/src/usbd_core.c ../middleware/usb_drivers/src/usbd_int.c ../middleware/usb_drivers/src/usbd_sdr.c ../middleware/usbd_class/cdc/cdc_class.c ../middleware/usbd_class/cdc/cdc_desc.c
INC+=-I../inc/app -I../inc/bsp -I../inc/drivers -I../inc/calibration -I../inc/telemetry -I../inc/fusion
SRCS+=src/bl_startup.c ../src/calibration/boot_startup.c ../src/calibration/gyro_startup_calibration.c ../src/calibration/fusion_settings.c ../src/calibration/acc_calibration.c ../src/drivers/lsm6dsv.c ../src/drivers/protocol.c ../src/drivers/can_protocol.c ../src/fusion/zaru_heading_hold.c $(LIB)/libraries/drivers/src/at32f423_spi.c $(LIB)/libraries/drivers/src/at32f423_scfg.c
SRCS+=../src/bsp/ws2812.c
OBJS:=$(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) $(BUILD)/startup_at32f423.o
vpath %.c src $(LIB)/libraries/cmsis/cm4/device_support $(LIB)/libraries/drivers/src ../middleware/usb_drivers/src ../middleware/usbd_class/cdc
vpath %.c ../src/calibration ../src/drivers ../src/fusion
vpath %.c ../src/bsp
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
$(BUILD):; mkdir -p $(BUILD)
$(BUILD)/%.o: %.c | $(BUILD); $(CC) $(CFLAGS) $(INC) -c $< -o $@
$(BUILD)/startup_at32f423.o: $(LIB)/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s | $(BUILD); $(CC) $(CFLAGS) $(INC) -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJS) linker/AT32F423_boot.ld; $(CC) $(CFLAGS) -Tlinker/AT32F423_boot.ld -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map --specs=nano.specs --specs=nosys.specs $(OBJS) -lm -o $@
$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf; $(OBJCOPY) -O ihex $< $@
$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf; $(OBJCOPY) -O binary $< $@
clean:; Remove-Item -Recurse -Force $(BUILD)
-include $(OBJS:.o=.d)
.PHONY: all clean
