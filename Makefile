# attiny85-sswitch
#
# Three targets, deliberately:
#   make firmware   cross-compile the AVR firmware
#   make test       host-side tests, no hardware
#   make flash      push firmware to a Digispark
#
# The repo-local toolchain from scripts/bootstrap-toolchain.sh goes on PATH
# automatically if it is present, so `make firmware` works after bootstrapping
# without exporting anything.

TOOLCHAIN := $(CURDIR)/.toolchain/opt/usr/bin
ifneq ($(wildcard $(TOOLCHAIN)/avr-gcc),)
export PATH := $(TOOLCHAIN):$(PATH)
endif

CC      ?= cc
CFLAGS  ?= -std=gnu99 -Wall -Wextra -Werror
INCLUDES = -Itests/hoststubs -Ifirmware/src

PYTHON ?= python3

.PHONY: all firmware test test-protocol test-descriptors test-vendor test-cli \
        flash size clean toolchain check

all: firmware

# ---------------------------------------------------------------- firmware --

#
# Pass the PATH down explicitly. `export PATH` in this file does not reach a
# sub-make's environment in all make implementations, and the result is a
# confusing "avr-gcc: not found" from firmware/Makefile after a bootstrap
# that worked perfectly well.
#
firmware:
	@$(MAKE) -C firmware PATH="$(PATH)"

size:
	@$(MAKE) -C firmware size PATH="$(PATH)"

flash: firmware
	scripts/flash.sh

# ------------------------------------------------------------------- tests --

#
# test-protocol compiles firmware/src/channels.c, proto.c and reply.c a second
# time as host code against tests/hoststubs/, with PORTB/DDRB/TCCR0A as plain
# variables. The logic under test is the logic that gets flashed; only the USB
# transport underneath is absent.
#
test-protocol: build/protocol_test
	@./build/protocol_test

build/protocol_test: tests/protocol_test.c tests/hoststubs/usbdrv.h \
                     tests/hoststubs/usbstub.c \
                     tests/hoststubs/avr/io.h tests/hoststubs/avr/pgmspace.h \
                     firmware/src/channels.c firmware/src/channels.h \
                     firmware/src/proto.c firmware/src/proto.h \
                     firmware/src/reply.c firmware/src/reply.h \
                     firmware/src/board.h firmware/src/config.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tests/protocol_test.c

#
# test-cli runs tools/sswitch against tools/protocol_sim.py, a test double on a
# pty. Verifies the host side: arguments, English translation, exit codes.
#
test-cli:
	$(PYTHON) -m unittest discover -s tests -v

#
# test-descriptors walks the USB descriptors out of the compiled ELF and
# checks them against the CDC-ACM requirements. Descriptors are hand-written
# byte arrays, which is exactly the kind of thing that is right in every
# individual byte and wrong in total length, endpoint type, or a field count.
# Those are the bugs that produce a device which never appears at
# /dev/ttyACM*, with no hint why.
#
# This one has already earned its keep: it caught a missing bNumInterfaces
# byte in the configuration header that would have stopped enumeration.
#
test-descriptors: firmware
	@$(PYTHON) tools/check_descriptors.py firmware/build/attiny85-sswitch.elf

#
# test-vendor guards the vendored V-USB. A nested .git there does not fail
# loudly -- git turns the directory into a gitlink, the commit succeeds, and
# the driver files never enter history. The next clone then has an empty
# vendor/v-usb and a build failure pointing at usbdrv.h.
#
test-vendor:
	@scripts/check-vendor.sh

test: test-protocol test-descriptors test-vendor test-cli

# ------------------------------------------------------------------- tools --

toolchain:
	scripts/bootstrap-toolchain.sh

#
# What CI should run: build, budget check, descriptor check, all tests.
#
check: firmware
	@command -v avr-gcc >/dev/null 2>&1 || \
		{ echo "avr-gcc not on PATH -- run: make toolchain"; exit 1; }
	@$(MAKE) test PATH="$(PATH)"

clean:
	@$(MAKE) -C firmware clean PATH="$(PATH)"
	rm -rf build
