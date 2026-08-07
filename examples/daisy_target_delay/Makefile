TARGET = daisy_target_delay

CPP_SOURCES = daisy_target_delay.cpp
CPP_STANDARD = -std=gnu++17
OPT ?= -O3
APP_TYPE ?= BOOT_SRAM

LIBDAISY_DIR ?= ../../external/libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

C_INCLUDES += -I../../src

BOARD ?= POD
AUDIO_BLOCK_SIZE ?= 48
ENABLE_PROFILING ?= 1

ifeq ($(BOARD),POD)
C_DEFS += -DPHH_DAISY_BOARD_POD
else ifeq ($(BOARD),FIELD)
C_DEFS += -DPHH_DAISY_BOARD_FIELD
else
$(error Unsupported BOARD='$(BOARD)'; use POD or FIELD)
endif

C_DEFS += -DPHH_AUDIO_BLOCK_SIZE=$(AUDIO_BLOCK_SIZE)
C_DEFS += -DPHH_ENABLE_PROFILING=$(ENABLE_PROFILING)

include $(SYSTEM_FILES_DIR)/Makefile
