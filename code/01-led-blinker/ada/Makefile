# Makefile — LED Blinker (Ada / STM32F446)

PREFIX	= arm-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

SRCS_DIR = src
OBJS_DIR = obj

SRCS    = $(wildcard $(SRCS_DIR)/*.c)
OBJS    = $(SRCS:$(SRCS_DIR)/%.c=$(OBJS_DIR)/%.o)

PROJECT  = led_blinker.gpr
TARGET  = led-blinker

RENODE := renode
RENODE_PLATFORM = ../renode/nucleo-f446re.repl
RENODE_SCRIPT = ../renode/led-blinker.resc

all: $(TARGET).bin size

-include $(subst .o,.d,$(OBJS))

$(TARGET).elf: $(OBJS)
	gprbuild -P $(PROJECT)
	cp $(OBJS_DIR)/$@ .

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
	rm -f $(OBJS_DIR)/* $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all clean renode size
