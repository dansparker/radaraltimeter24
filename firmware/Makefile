# STM32F405RG firmware of the 24 GHz radar altimeter
#
#   make                       build (needs arm-none-eabi-gcc in PATH)
#   make CMSIS=<dir>           CMSIS root with Include/ and Device/ST/STM32F4xx/Include
#   make flash                 program with st-flash (optional)
#
# The CMSIS headers can be fetched with ../tools/fetch_cmsis.sh (default dir).

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

CMSIS   ?= ../build/cmsis
BUILD   ?= build
TARGET  := radar_altimeter

RDSP    := ../lib/rdsp
SRCS    := $(wildcard $(RDSP)/src/*.c) $(wildcard app/*.c) $(wildcard bsp/*.c) $(wildcard src/*.c)
OBJS    := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
VPATH   := $(RDSP)/src app bsp src

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    := -DSTM32F405xx '-DALT_CCM=__attribute__((section(".ccmbss")))'
INCS    := -I$(RDSP)/include -Iapp -Ibsp -Isrc \
           -I$(CMSIS)/Include -I$(CMSIS)/Device/ST/STM32F4xx/Include
CFLAGS  := $(MCU) $(DEFS) $(INCS) -std=c99 -O2 -g3 -ffunction-sections -fdata-sections \
           -fno-math-errno -Wall -Wextra -Wdouble-promotion -Wshadow -Wno-unused-parameter \
           -MMD -MP
LDFLAGS := $(MCU) -Tbsp/stm32f405rg.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage
LDLIBS  := -lm

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD):
	mkdir -p $(BUILD)

flash: $(BUILD)/$(TARGET).bin
	st-flash --reset write $< 0x08000000

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash
-include $(OBJS:.o=.d)
