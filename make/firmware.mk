# AT32F423KCU7-4 + LSM6DSV/IST8310 2 kHz official Full VQF

ESKF8K   ?= 0
ESKF_VALIDATE ?= 0
ESKF_HOTSPOT ?= 0
ESKF_HOT_GROUP ?= 0
ESKF_CAPTURE_OPT ?= 0
ESKF_CLOCK_OPT ?= 0
PROTOCOL_CRC_TABLE ?= 0
UART_RX_DMA ?= 0
FLASH_TRACE ?= 0
SRAM_CAPTURE ?= 0
USB_DIAG ?= 0
USB_TX_OWNERSHIP_FIX ?= 0
USB_TX_CHAIN ?= 0
USB_FIFO_FIX ?= 0
USB_BUFFER_FIX ?= 0
USB_PACK_FIX ?= 0
UART_TX_DIAG ?= 0
UART_TX_QUEUE_FIX ?= 0
CAN_TX_FAST ?= 0
COMM_POLL_FAST ?= 0
UART_TX_SERVICE_2K ?= 0
UART_TX_BATCH ?= 0
UART_TX_PAD_TEST ?= 0
STATIC6_DIAG ?= 0
ESKF_Q_COMP ?= 0
ESKF_Q_COMP_FAST ?= 0
VOFA_6AXIS ?= 0
ESKF_REST_RELAX ?= 0
STARTUP_BIAS_SAVE ?= 1
TEMP_FLASH_AUTO_SAVE ?= 1
EXTRA_CFLAGS += -DAPP_GYR_STARTUP_BIAS_SAVE_ENABLE=$(STARTUP_BIAS_SAVE)
EXTRA_CFLAGS += -DAPP_TEMP_FLASH_AUTO_SAVE_ENABLE=$(TEMP_FLASH_AUTO_SAVE)
ifeq ($(ESKF_REST_RELAX),1)
ifneq ($(ESKF8K)$(VOFA_6AXIS)$(ESKF_Q_COMP)$(STATIC6_DIAG),1100)
$(error ESKF_REST_RELAX requires Q math, six-axis output and diagnostics OFF)
endif
EXTRA_CFLAGS += -DAPP_ESKF_REST_RELAX=1
endif
ifneq ($(filter 1 2,$(ESKF_Q_COMP_FAST)),)
ifneq ($(ESKF_Q_COMP)$(STATIC6_DIAG)$(VOFA_6AXIS),111)
$(error ESKF_Q_COMP_FAST requires compensated diagnostic six-axis build)
endif
EXTRA_CFLAGS += -DAPP_ESKF_Q_COMP_FAST=$(ESKF_Q_COMP_FAST)
endif
ifeq ($(VOFA_6AXIS),1)
ifneq ($(UART_TX_SERVICE_2K)$(ESKF8K),11)
$(error VOFA_6AXIS requires the verified Q dual8k build)
endif
EXTRA_CFLAGS += -DAPP_VOFA_6AXIS=1
endif
ifeq ($(ESKF_Q_COMP),1)
ifneq ($(ESKF8K),1)
$(error ESKF_Q_COMP requires ESKF8K=1)
endif
EXTRA_CFLAGS += -DAPP_ESKF_Q_COMP=1
endif
ifeq ($(STATIC6_DIAG),1)
ifneq ($(UART_TX_SERVICE_2K)$(ESKF8K),11)
$(error STATIC6_DIAG requires the verified Q build flags)
endif
EXTRA_CFLAGS += -DAPP_STATIC6_DIAG=1
SRCS_STATIC6 = $(SRC_DIR)/bsp/static6_diag.c
endif
ifeq ($(UART_TX_PAD_TEST),1)
ifneq ($(UART_TX_QUEUE_FIX)$(UART_TX_SERVICE_2K)$(UART_TX_BATCH),110)
$(error UART_TX_PAD_TEST requires queue fix, 2k service and no batching)
endif
EXTRA_CFLAGS += -DAPP_UART_TX_PAD_TEST=1
endif
ifeq ($(UART_TX_BATCH),1)
ifneq ($(UART_TX_SERVICE_2K),1)
$(error UART_TX_BATCH requires UART_TX_SERVICE_2K=1)
endif
EXTRA_CFLAGS += -DAPP_UART_TX_BATCH=1
endif
ifeq ($(UART_TX_SERVICE_2K),1)
ifneq ($(UART_TX_QUEUE_FIX),1)
$(error UART_TX_SERVICE_2K requires UART_TX_QUEUE_FIX=1)
endif
EXTRA_CFLAGS += -DAPP_UART_TX_SERVICE_2K=1
endif
ifeq ($(COMM_POLL_FAST),1)
ifneq ($(CAN_TX_FAST),1)
$(error COMM_POLL_FAST requires CAN_TX_FAST=1)
endif
EXTRA_CFLAGS += -DAPP_COMM_POLL_FAST=1
endif
ifeq ($(UART_TX_QUEUE_FIX),1)
ifneq ($(UART_TX_DIAG),1)
$(error UART_TX_QUEUE_FIX requires UART_TX_DIAG=1)
endif
EXTRA_CFLAGS += -DAPP_UART_TX_QUEUE_FIX=1 -DAPP_GYR_TEMP_COMP_ENABLE=0 -DAPP_GYR_TEMP_LEARNING_ENABLE=0
endif
ifeq ($(CAN_TX_FAST),1)
ifneq ($(UART_TX_QUEUE_FIX),1)
$(error CAN_TX_FAST requires UART_TX_QUEUE_FIX=1)
endif
EXTRA_CFLAGS += -DAPP_CAN_TX_FAST=1
endif
ifeq ($(UART_TX_DIAG),1)
ifneq ($(USB_PACK_FIX),1)
$(error UART_TX_DIAG requires the verified L USB_PACK_FIX=1 baseline)
endif
EXTRA_CFLAGS += -DAPP_UART_TX_DIAG=1
endif
ifeq ($(USB_PACK_FIX),1)
ifneq ($(USB_BUFFER_FIX),1)
$(error USB_PACK_FIX requires USB_BUFFER_FIX=1)
endif
EXTRA_CFLAGS += -DAPP_USB_PACK_FIX=1
endif
ifeq ($(USB_BUFFER_FIX),1)
ifneq ($(USB_FIFO_FIX),1)
$(error USB_BUFFER_FIX requires USB_FIFO_FIX=1)
endif
EXTRA_CFLAGS += -DAPP_USB_BUFFER_FIX=1
endif
ifeq ($(USB_FIFO_FIX),1)
ifneq ($(USB_TX_CHAIN),1)
$(error USB_FIFO_FIX experimental image requires USB_TX_CHAIN=1)
endif
EXTRA_CFLAGS += -DAPP_USB_FIFO_FIX=1
endif
ifeq ($(USB_TX_CHAIN),1)
ifneq ($(USB_TX_OWNERSHIP_FIX),1)
$(error USB_TX_CHAIN requires USB_TX_OWNERSHIP_FIX=1)
endif
EXTRA_CFLAGS += -DAPP_USB_TX_CHAIN=1
endif
ifeq ($(USB_TX_OWNERSHIP_FIX),1)
ifneq ($(USB_DIAG),1)
$(error USB_TX_OWNERSHIP_FIX requires USB_DIAG=1)
endif
EXTRA_CFLAGS += -DAPP_USB_TX_OWNERSHIP_FIX=1
endif
ifeq ($(USB_DIAG),1)
ifneq ($(ESKF8K)$(SRAM_CAPTURE),11)
$(error USB_DIAG requires ESKF8K=1 SRAM_CAPTURE=1)
endif
EXTRA_CFLAGS += -DAPP_USB_DIAG=1
endif
TARGET   ?= $(if $(filter 1,$(ESKF8K)),lsm6dsv_eskf8k,lsm6dsv_spi_test)
MCU      := cortex-m4
CHIP     := AT32F423KCU7_4

LIB      := AT32F423_Firmware_Library
SRC_DIR  := src
APP_MAIN ?= app/main.c
INC_DIR  := inc
BUILD    = build/$(TARGET)/$(if $(filter 1,$(DEBUG_BUILD)),debug,release)-$(if $(filter 1,$(SIX_AXIS)),6axis,9axis)

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

LDSCRIPT := linker/AT32F423_app.ld
ifeq ($(SRAM_CAPTURE),1)
ifneq ($(ESKF8K)$(ESKF_VALIDATE)$(ESKF_CAPTURE_OPT)$(FLASH_TRACE),1111)
$(error SRAM_CAPTURE requires ESKF8K=1 ESKF_VALIDATE=1 ESKF_CAPTURE_OPT=1 FLASH_TRACE=1)
endif
ifneq ($(ESKF_HOTSPOT),0)
$(error SRAM_CAPTURE cannot use intrusive hotspot banks)
endif
LDSCRIPT := linker/AT32F423_sram_capture.ld
EXTRA_CFLAGS += -DAPP_SRAM_CAPTURE=1 -DAPP_SRAM_SETTINGS_ONLY=1 -fstack-usage
endif
STARTUP  := $(LIB)/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s

SRCS := \
  $(SRCS_STATIC6) \
  $(SRC_DIR)/$(APP_MAIN) \
  $(SRC_DIR)/bsp/bsp.c \
  $(SRC_DIR)/drivers/lsm6dsv.c \
  $(SRC_DIR)/drivers/ist8310.c \
  $(SRC_DIR)/calibration/gyro_bias_history.c \
  $(SRC_DIR)/calibration/gyro_temp_comp.c \
  $(SRC_DIR)/calibration/gyro_temp_learning.c \
  $(SRC_DIR)/calibration/gyro_startup_calibration.c \
  $(SRC_DIR)/calibration/boot_startup.c \
  $(SRC_DIR)/calibration/acc_calibration.c \
  $(SRC_DIR)/calibration/acc_six_face.c \
  $(SRC_DIR)/calibration/fusion_settings.c \
  $(SRC_DIR)/calibration/vqf_static_cal.c \
  $(SRC_DIR)/fusion/attitude_output.c \
  $(SRC_DIR)/fusion/zaru_heading_hold.c \
  $(SRC_DIR)/bsp/ws2812.c \
  $(SRC_DIR)/drivers/can_test.c \
  $(SRC_DIR)/drivers/can_protocol.c \
  $(SRC_DIR)/drivers/usb_cdc.c \
  middleware/usb_drivers/src/usb_core.c \
  middleware/usb_drivers/src/usbd_core.c \
  middleware/usb_drivers/src/usbd_int.c \
  middleware/usb_drivers/src/usbd_sdr.c \
  middleware/usbd_class/cdc/cdc_class.c \
  middleware/usbd_class/cdc/cdc_desc.c \
  $(SRC_DIR)/drivers/protocol.c \
  $(SRC_DIR)/drivers/inertial_stream.c \
  $(SRC_DIR)/drivers/data_capture.c \
  $(SRC_DIR)/drivers/user_bl_update.c \
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

ifeq ($(SRAM_CAPTURE),1)
SRCS += $(SRC_DIR)/bsp/heap_guard.c
SRCS += $(SRC_DIR)/bsp/flash_gate.c
endif

CPPSRCS := \
  $(SRC_DIR)/fusion/ekf.cpp \
  $(SRC_DIR)/fusion/vqf_wrapper.cpp \
  $(SRC_DIR)/fusion/vqf_full.cpp

ifeq ($(ESKF8K),1)
SRCS += $(SRC_DIR)/drivers/imu8k_capture.c
CPPSRCS += $(SRC_DIR)/fusion/eskf8k.cpp
EXTRA_CFLAGS += -DAPP_EXPERIMENTAL_ESKF8K=1 -O3
ifeq ($(ESKF_CAPTURE_OPT),1)
EXTRA_CFLAGS += -DAPP_ESKF_CAPTURE_OPT=1
endif
ifeq ($(ESKF_CLOCK_OPT),1)
EXTRA_CFLAGS += -DAPP_TIMEBASE_FAST=1
endif
ifeq ($(PROTOCOL_CRC_TABLE),1)
EXTRA_CFLAGS += -DAPP_PROTOCOL_CRC_TABLE=1
endif
ifeq ($(UART_RX_DMA),1)
EXTRA_CFLAGS += -DAPP_UART_RX_DMA=1
endif
ifeq ($(FLASH_TRACE),1)
EXTRA_CFLAGS += -DAPP_FLASH_TRACE=1
SRCS += $(SRC_DIR)/bsp/flash_sram.c
endif
ifeq ($(SRAM_CAPTURE),1)
SRCS += $(SRC_DIR)/bsp/sram_capture.c
endif
ifeq ($(ESKF_VALIDATE),1)
SRCS += $(SRC_DIR)/bsp/eskf8k_validation.c
EXTRA_CFLAGS += -DAPP_ESKF8K_VALIDATE=1
ifeq ($(ESKF_HOTSPOT),1)
SRCS += $(SRC_DIR)/bsp/eskf8k_hotspot.c
EXTRA_CFLAGS += -DAPP_ESKF8K_HOTSPOT=1 -DAPP_ESKF8K_HOT_GROUP=$(ESKF_HOT_GROUP)
endif
endif
endif

INCLUDES := \
  -I$(INC_DIR) \
  -I$(INC_DIR)/app \
  -I$(INC_DIR)/bsp \
  -I$(INC_DIR)/calibration \
  -I$(INC_DIR)/config \
  -I$(INC_DIR)/drivers \
  -I$(INC_DIR)/fusion \
  -I$(INC_DIR)/telemetry \
  -I$(LIB)/libraries/cmsis/cm4/core_support \
  -I$(LIB)/libraries/cmsis/cm4/device_support \
  -I$(LIB)/libraries/drivers/inc \
  -Imiddleware/usb_drivers/inc \
  -Imiddleware/usbd_class/cdc

# Debug/production selects only the factory fast-start default. Both startup
# paths are compiled; saved host settings override this default after reboot.
DEBUG_BUILD ?= 0
SIX_AXIS ?= 0
FAST_START_CFLAG := -DAPP_GYR_FAST_START_ENABLE=$(DEBUG_BUILD)
# SIX_AXIS=1 disables magnetometer fusion and forces the runtime mode to 6-axis.
MAG_FUSION_CFLAG := -DAPP_MAG_FUSION_ENABLE=$(if $(filter 1,$(SIX_AXIS)),0,1)

CFLAGS := -mcpu=$(MCU) -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -D$(CHIP) -DUSE_STDPERIPH_DRIVER -DVECT_TAB_OFFSET=0x8000 \
  $(INCLUDES) \
  $(FAST_START_CFLAG) \
  $(MAG_FUSION_CFLAG) \
  -O1 -g -Wall -ffunction-sections -fdata-sections \
  -fno-common -fno-builtin $(EXTRA_CFLAGS)

CXXFLAGS := $(CFLAGS) -O3 -DVQF_SINGLE_PRECISION -std=gnu++14 -fno-exceptions -fno-rtti
ifeq ($(USER_BL_UPDATER),1)
CFLAGS += -DAPP_USER_BL_UPDATE -Ibuild/user-bl-updater
CXXFLAGS += -DAPP_USER_BL_UPDATE -Ibuild/user-bl-updater
endif

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
  $(SRC_DIR)/calibration \
  $(SRC_DIR)/drivers \
  $(SRC_DIR)/fusion \
  $(LIB)/libraries/cmsis/cm4/device_support \
  $(LIB)/libraries/drivers/src \
  middleware/usb_drivers/src \
  middleware/usbd_class/cdc
vpath %.cpp $(SRC_DIR)/fusion

.DEFAULT_GOAL := all
.PHONY: all bootloader clean

bootloader:
	$(MAKE) -C bootloader -B

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/startup_at32f423.o: $(STARTUP) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

