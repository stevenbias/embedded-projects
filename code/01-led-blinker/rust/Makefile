# Makefile — LED Blinker (Rust / STM32F405)

PREFIX	= arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

TARGET  = led-blinker

RENODE := renode
RENODE_PLATFORM = ../renode/nucleo-f446re.repl
RENODE_SCRIPT = ../renode/led-blinker.resc

$(OBJS_DIR):
	mkdir -p $@

all: $(TARGET).elf

$(TARGET).elf:
	cargo build --release
	cp target/thumbv7m-none-eabi/release/$(TARGET) $@

renode:
	renode -e '$$bin=@$(TARGET).elf; $$repl=@$(RENODE_PLATFORM); include @$(RENODE_SCRIPT)'

size: $(TARGET).elf
	$(SIZE) $<

clean:
	cargo clean
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all clean renode size
