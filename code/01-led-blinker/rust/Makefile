# Makefile — LED Blinker (Rust / STM32F446RE)

PREFIX	= arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

TARGET  = led-blinker

RENODE 					= renode
RENODE_PLATFORM = ../renode/nucleo-f446re.repl
RENODE_SCRIPT 	= ../renode/led-blinker.resc

# Variables specific to Rust
TARGET_TRIPLET = thumbv7em-none-eabihf
BUILD_PROFILE = --release
BUILD_DIR = release

all: $(TARGET).bin size

debug: BUILD_DIR = debug
debug: BUILD_PROFILE = 
debug: all

$(TARGET).elf:
	cargo build $(BUILD_PROFILE)
	cp target/$(TARGET_TRIPLET)/$(BUILD_DIR)/$(TARGET) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

erase:
	st-flash erase

flash: $(TARGET).bin
	st-flash write $(TARGET).bin 0x08000000

renode: $(TARGET).elf
	renode -e '$$bin=@$<; $$repl=@$(RENODE_PLATFORM); include @$(RENODE_SCRIPT)'

size: $(TARGET).elf
	$(SIZE) $<

clean:
	cargo clean
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all clean renode size
