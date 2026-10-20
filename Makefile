# Default to dual-8-kHz ESKF with a RAM-only fresh startup bias.
# The original VQF/MEKF build is available with PROFILE=legacy.
PROFILE ?= eskf8k

ifeq ($(PROFILE),eskf8k)
include make/eskf8k-ram-bias.mk
else ifeq ($(PROFILE),eskf8k-j)
include make/eskf8k-j.mk
else ifeq ($(PROFILE),legacy)
# Keep the original firmware.mk defaults and selectable VQF/MEKF paths.
else
$(error Unknown PROFILE '$(PROFILE)'; use eskf8k, eskf8k-j or legacy)
endif

include make/firmware.mk
