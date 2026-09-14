# AT32F423KCU7-4 + LSM6DSV/IST8310 2 kHz official Full VQF

TARGET   ?= lsm6dsv_spi_test
MCU      := cortex-m4
CHIP     := AT32F423KCU7_4

LIB      := AT32F423_Firmware_Library
SRC_DIR  := src
APP_MAIN ?= app/main.c
INC_DIR  := inc
BUILD    := build

CC       := arm-none-eabi-gcc
CXX      := arm-none-eabi-g++
OBJCOPY  := arm-none-eabi-objcopy
SIZE     := arm-none-eabi-size

ifeq ($(OS),Windows_NT)
PIO_GCC  := $(USERPROFILE)/.platformio/packages/toolchain-gccarmnoneeabi/bin
else
PIO_GCC  := $(HOME)/.platformio/packages/toolchain-gccarmnoneeabi/bin
endif
ifneq ($(wildcard $(PIO_GCC)/arm-none-eabi-gcc.exe),)
CC      := $(PIO_GCC)/arm-none-eabi-gcc
CXX     := $(PIO_GCC)/arm-none-eabi-g++
OBJCOPY := $(PIO_GCC)/arm-none-eabi-objcopy
SIZE    := $(PIO_GCC)/arm-none-eabi-size
endif
ifneq ($(wildcard $(PIO_GCC)/arm-none-eabi-gcc),)
CC      := $(PIO_GCC)/arm-none-eabi-gcc
CXX     := $(PIO_GCC)/arm-none-eabi-g++
OBJCOPY := $(PIO_GCC)/arm-none-eabi-objcopy
SIZE    := $(PIO_GCC)/arm-none-eabi-size
endif

LDSCRIPT := $(LIB)/libraries/cmsis/cm4/device_support/startup/gcc/linker/AT32F423xC_FLASH.ld
STARTUP  := $(LIB)/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s

SRCS := \
  $(SRC_DIR)/$(APP_MAIN) \
  $(SRC_DIR)/bsp/bsp.c \
  $(SRC_DIR)/drivers/lsm6dsv.c \
  $(SRC_DIR)/drivers/ist8310.c \
  $(SRC_DIR)/bsp/ws2812.c \
  $(SRC_DIR)/drivers/can_test.c \
  $(SRC_DIR)/drivers/usb_cdc.c \
  middleware/usb_drivers/src/usb_core.c \
  middleware/usb_drivers/src/usbd_core.c \
  middleware/usb_drivers/src/usbd_int.c \
  middleware/usb_drivers/src/usbd_sdr.c \
  middleware/usbd_class/cdc/cdc_class.c \
  middleware/usbd_class/cdc/cdc_desc.c \
  $(SRC_DIR)/drivers/protocol.c \
  $(SRC_DIR)/bsp/at32f423_clock.c \
  $(SRC_DIR)/bsp/at32f423_int.c \
  $(LIB)/libraries/cmsis/cm4/device_support/system_at32f423.c \
  $(LIB)/libraries/drivers/src/at32f423_crm.c \
  $(LIB)/libraries/drivers/src/at32f423_gpio.c \
  $(LIB)/libraries/drivers/src/at32f423_misc.c \
  $(LIB)/libraries/drivers/src/at32f423_spi.c \
  $(LIB)/libraries/drivers/src/at32f423_usart.c \
  $(LIB)/libraries/drivers/src/at32f423_flash.c \
  $(LIB)/libraries/drivers/src/at32f423_pwc.c \
  $(LIB)/libraries/drivers/src/at32f423_dma.c \
  $(LIB)/libraries/drivers/src/at32f423_can.c \
  $(LIB)/libraries/drivers/src/at32f423_usb.c \
  $(LIB)/libraries/drivers/src/at32f423_acc.c

CPPSRCS := \
  $(SRC_DIR)/fusion/vqf_wrapper.cpp \
  $(SRC_DIR)/fusion/vqf_full.cpp

INCLUDES := \
  -I$(INC_DIR) \
  -I$(INC_DIR)/app \
  -I$(INC_DIR)/bsp \
  -I$(INC_DIR)/calibration \
  -I$(INC_DIR)/config \
  -I$(INC_DIR)/diagnostics \
  -I$(INC_DIR)/drivers \
  -I$(INC_DIR)/fusion \
  -I$(INC_DIR)/telemetry \
  -I$(LIB)/libraries/cmsis/cm4/core_support \
  -I$(LIB)/libraries/cmsis/cm4/device_support \
  -I$(LIB)/libraries/drivers/inc \
  -Imiddleware/usb_drivers/inc \
  -Imiddleware/usbd_class/cdc

CFLAGS := -mcpu=$(MCU) -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -D$(CHIP) -DUSE_STDPERIPH_DRIVER \
  $(INCLUDES) \
  -O1 -g -Wall -ffunction-sections -fdata-sections \
  -fno-common -fno-builtin $(EXTRA_CFLAGS)

CXXFLAGS := $(CFLAGS) -O3 -DVQF_SINGLE_PRECISION -std=gnu++14 -fno-exceptions -fno-rtti

LDFLAGS := -mcpu=$(MCU) -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
  --specs=nano.specs --specs=nosys.specs -lm

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) \
        $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(CPPSRCS))) \
        $(BUILD)/startup_at32f423.o

DEPS := $(OBJS:.o=.d)
DEPFLAGS = -MMD -MP -MF $(@:.o=.d)
-include $(DEPS)

vpath %.c $(SRC_DIR)/app \
  $(SRC_DIR)/bsp \
  $(SRC_DIR)/drivers \
  $(SRC_DIR)/fusion \
  $(SRC_DIR)/diagnostics \
  $(LIB)/libraries/cmsis/cm4/device_support \
  $(LIB)/libraries/drivers/src \
  middleware/usb_drivers/src \
  middleware/usbd_class/cdc \
vpath %.cpp $(SRC_DIR)/fusion

.DEFAULT_GOAL := all
.PHONY: all clean spi-matrix spi-safe-probe spi-observe spi-freq-sweep safe-idle ist8310

spi-matrix:
	$(MAKE) TARGET=spi_matrix APP_MAIN=diagnostics/spi_matrix_main.c all

spi-safe-probe:
	$(MAKE) TARGET=spi_safe_probe APP_MAIN=diagnostics/spi_safe_probe_main.c all

spi-observe:
	$(MAKE) TARGET=spi_observe APP_MAIN=diagnostics/spi_observe_main.c all

spi-freq-sweep:
	$(MAKE) TARGET=spi_freq_sweep APP_MAIN=diagnostics/spi_freq_sweep_main.c all

safe-idle:
	$(MAKE) TARGET=safe_idle APP_MAIN=diagnostics/safe_idle_main.c all

ist8310:
	$(MAKE) TARGET=ist8310_test APP_MAIN=diagnostics/ist8310_test_main.c all

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/startup_at32f423.o: $(STARTUP) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)




