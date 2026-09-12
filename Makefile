# AT32F423KCU7-4 + LSM6DSV/IST8310 2 kHz official Full VQF

TARGET   ?= lsm6dsv_spi_test
MCU      := cortex-m4
CHIP     := AT32F423KCU7_4

LIB      := AT32F423_Firmware_Library
SRC_DIR  := src
APP_MAIN ?= main.c
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
  $(SRC_DIR)/bsp.c \
  $(SRC_DIR)/lsm6dsv.c \
  $(SRC_DIR)/ist8310.c \
  $(SRC_DIR)/ws2812.c \
  $(SRC_DIR)/at32f423_clock.c \
  $(SRC_DIR)/at32f423_int.c \
  $(LIB)/libraries/cmsis/cm4/device_support/system_at32f423.c \
  $(LIB)/libraries/drivers/src/at32f423_crm.c \
  $(LIB)/libraries/drivers/src/at32f423_gpio.c \
  $(LIB)/libraries/drivers/src/at32f423_misc.c \
  $(LIB)/libraries/drivers/src/at32f423_spi.c \
  $(LIB)/libraries/drivers/src/at32f423_usart.c \
  $(LIB)/libraries/drivers/src/at32f423_flash.c \
  $(LIB)/libraries/drivers/src/at32f423_pwc.c \
  $(LIB)/libraries/drivers/src/at32f423_dma.c

CPPSRCS := \
  $(SRC_DIR)/vqf_wrapper.cpp \
  $(SRC_DIR)/vqf_full.cpp

INCLUDES := \
  -I$(INC_DIR) \
  -I$(LIB)/libraries/cmsis/cm4/core_support \
  -I$(LIB)/libraries/cmsis/cm4/device_support \
  -I$(LIB)/libraries/drivers/inc

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

vpath %.c $(SRC_DIR) \
  $(LIB)/libraries/cmsis/cm4/device_support \
  $(LIB)/libraries/drivers/src

vpath %.cpp $(SRC_DIR)

.DEFAULT_GOAL := all
.PHONY: all clean spi-matrix spi-safe-probe spi-observe spi-freq-sweep safe-idle

spi-matrix:
	$(MAKE) TARGET=spi_matrix APP_MAIN=spi_matrix_main.c all

spi-safe-probe:
	$(MAKE) TARGET=spi_safe_probe APP_MAIN=spi_safe_probe_main.c all

spi-observe:
	$(MAKE) TARGET=spi_observe APP_MAIN=spi_observe_main.c all

spi-freq-sweep:
	$(MAKE) TARGET=spi_freq_sweep APP_MAIN=spi_freq_sweep_main.c all

safe-idle:
	$(MAKE) TARGET=safe_idle APP_MAIN=safe_idle_main.c all

ist8310:
	$(MAKE) TARGET=ist8310_test APP_MAIN=ist8310_test_main.c all

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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






