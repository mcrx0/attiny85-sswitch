# Firmware for attiny85-sswitch.
#
# Bare-metal AVR. V-USB provides USB; nothing else does.

MCU      = attiny85
F_CPU    = 16500000

# Fail with an instruction rather than "avr-gcc: No such file or directory".
# Building through the top-level Makefile puts the repo-local toolchain on
# PATH; building here directly does not.
ifeq ($(shell command -v avr-gcc 2>/dev/null),)
$(error avr-gcc not found. Either build from the repo root -- "make firmware", which adds .toolchain/opt/usr/bin to PATH -- or run scripts/bootstrap-toolchain.sh)
endif

SRC_DIR   = src
VUSB_DIR  = vendor/v-usb/usbdrv
BUILD_DIR = build
TARGET    = $(BUILD_DIR)/attiny85-sswitch

# -funsigned-char and -funsigned-bitfields are not cosmetic: V-USB's uchar
# arithmetic depends on char being unsigned on AVR, which is not the
# compiler default for that target. Dropping them produces a device that
# compiles cleanly and then misbehaves at runtime.
CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL -Os -std=gnu99 \
         -Wall -Wextra \
         -funsigned-char -funsigned-bitfields \
         -ffunction-sections -fdata-sections

LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

C_SRC  = $(SRC_DIR)/main.c $(SRC_DIR)/channels.c $(SRC_DIR)/proto.c \
         $(SRC_DIR)/reply.c $(VUSB_DIR)/usbdrv.c
S_SRC  = $(VUSB_DIR)/usbdrvasm.S
C_OBJ  = $(addprefix $(BUILD_DIR)/,$(C_SRC:.c=.o))
S_OBJ  = $(addprefix $(BUILD_DIR)/,$(S_SRC:.S=.o))
OBJ    = $(C_OBJ) $(S_OBJ)

CPPFLAGS = -I$(SRC_DIR) -I. -I$(VUSB_DIR)

# Cross-compiling, so the compiler is avr-gcc even when make's own CC is cc.
AVR_CC = avr-gcc
AVR_OBJCOPY = avr-objcopy
AVR_SIZE = avr-size

.PHONY: all clean size check-budget

all: $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(AVR_CC) $(LDFLAGS) -o $@ $(OBJ)

$(TARGET).hex: $(TARGET).elf
	$(AVR_OBJCOPY) -O ihex -R .eeprom $< $@
	$(AVR_OBJCOPY) -O binary -R .eeprom $< $(TARGET).bin

$(BUILD_DIR)/$(VUSB_DIR)/%.o: $(VUSB_DIR)/%.c
	@mkdir -p $(dir $@)
	$(AVR_CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/$(VUSB_DIR)/%.o: $(VUSB_DIR)/%.S
	@mkdir -p $(dir $@)
	$(AVR_CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(AVR_CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

size: $(TARGET).elf
	@$(AVR_SIZE) --format=avr --mcu=$(MCU) $(TARGET).elf
	@$(MAKE) --no-print-directory check-budget

# micronucleus's t85_default configuration puts the bootloader at 0x1A00,
# so the application gets 6656 bytes -- not the full 8192 of flash. A build
# that overruns 0x1A00 still links and produces a .hex that looks fine; it
# fails only when you flash it and the board comes back dead. Check here
# instead.
#
# Decimal (6656), not hex: this is compared with shell arithmetic, and `test`
# rejects 0x1A00 as a number.
BOOTLOADER_ADDRESS = 6656

check-budget: $(TARGET).elf
	@end=$$($(AVR_SIZE) --format=avr $(TARGET).elf | \
	        awk '/^Program:/ {print $$2}'); \
	 if [ -z "$$end" ]; then \
	     echo "FATAL: could not read the program size"; exit 1; \
	 elif [ "$$end" -gt $(BOOTLOADER_ADDRESS) ]; then \
	     echo "FATAL: firmware is $$end bytes, but the micronucleus"; \
	     echo "       bootloader starts at 0x1A00 ($(BOOTLOADER_ADDRESS) bytes)."; \
	     echo "       This links, and then bricks the board on flash."; \
	     exit 1; \
	 else \
	     echo "budget: $$end / $(BOOTLOADER_ADDRESS) bytes before the bootloader"; \
	 fi

clean:
	rm -rf $(BUILD_DIR)
