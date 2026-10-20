# Default to the frozen dual-8-kHz ESKF J firmware.
# The original VQF/MEKF build is available with PROFILE=legacy.
PROFILE ?= eskf8k

ifeq ($(PROFILE),eskf8k)
include make/eskf8k-j.mk
else ifeq ($(PROFILE),legacy)
# Keep the original firmware.mk defaults and selectable VQF/MEKF paths.
else
$(error Unknown PROFILE '$(PROFILE)'; use eskf8k or legacy)
endif

include make/firmware.mk
