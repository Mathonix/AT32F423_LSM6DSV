# 20261020a: retain fresh startup calibration, without temperature-model
# fallback persistence or automatic temperature-model saves.
TARGET ?= lsm6dsv_eskf8k_ram_bias
STARTUP_BIAS_SAVE ?= 0
TEMP_FLASH_AUTO_SAVE ?= 0
include make/eskf8k-j.mk
