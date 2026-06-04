# Makefile — LED Blinker (C / STM32F446)

PREFIX	= arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

SRCS_DIR = src
INCS_DIR = inc
OBJS_DIR = build

LINKER_SCRIPT = ../common/linker.ld
STARTUP_FILE = ../common/crt0.s

SRCS    = $(wildcard $(SRCS_DIR)/*.c)
OBJS    = $(SRCS:$(SRCS_DIR)/%.c=$(OBJS_DIR)/%.o)
OBJS    += $(OBJS_DIR)/crt0.o
# OBJS    += $(OBJS_DIR)/startup.o

# CPP stands for C PreProcessor
# -I is a preprocessor flag, not a compiler flag
CPPFLAGS := -I$(INCS_DIR)
OPTIM_FLAGS := -flto -Os
CFLAGS  = $(OPTIM_FLAGS) \
					-mcpu=cortex-m4 \
					-mthumb \
					-mfloat-abi=hard \
					-mfpu=fpv4-sp-d16 \
					-Wall \
					-Wextra \
					-Werror \
					-ffreestanding \
					-nostdlib
# Add switches to reduce generated code size
CFLAGS += -ffunction-sections \
					-fdata-sections \
					-Wl,--gc-sections \
					# -Wl,--print-memory-usage
	
ASFLAGS = -mcpu=cortex-m4 -mthumb

TARGET  = led-blinker

RENODE 					= renode
RENODE_PLATFORM = ../renode/nucleo-f446re.repl
RENODE_SCRIPT 	= ../renode/led-blinker.resc

all: $(TARGET).bin size

debug: OPTIM_FLAGS := -g3 -Og
debug: all

-include $(subst .o,.d,$(OBJS))

$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT)
	$(CC) $(CFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJS) -Wl,-Map=$(TARGET).map

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(OBJS_DIR)/%.d: $(SRCS_DIR)/%.c | $(OBJS_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MM -MMD -MF $@ -MP $<

$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.c | $(OBJS_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -MMD -o $@ $<

$(OBJS_DIR)/%.o: $(STARTUP_FILE) | $(OBJS_DIR)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(OBJS_DIR):
	mkdir -p $@

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
