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
        flash size clean toolchain check \
        docker-build docker-check docker-firmware docker-shell docker-clean

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

# ------------------------------------------------------------------- docker --
#
# A toolbox image, not a builder: the Dockerfile COPYs nothing and the working
# tree is bind-mounted in. That means no stale layer can ever disagree with your
# checkout, and no rebuild is needed after editing a source file.
#
# The image is identical in both directions -- `docker-build docker-check` and
# bare `make check` produce byte-identical firmware, because both use the same
# avr-gcc. See docker/README.md.
#
DOCKER        ?= docker
DOCKER_IMAGE  ?= attiny85-sswitch-build
DOCKERFILE    ?= docker/Dockerfile

#
# --user with the host uid, not whatever the image defaults to. The mount is
# owned by you; a container writing as any other uid leaves every build product
# in firmware/build/ owned by somebody else on the host, and the next rm -rf
# needs sudo. HOME is redirected because the host uid has no passwd entry in
# the image.
#
DOCKER_ARGS = \
	--user "$(shell id -u):$(shell id -g)" \
	-e HOME=/tmp \
	-v "$(CURDIR):/src" -w /src

#
# Flags must precede the image name. Everything after it is the command to run
# inside the container, so `$(DOCKER_RUN) -it bash` does not ask for an
# interactive terminal -- it asks the container to exec a program literally
# named "-it", and fails with:
#     exec: "-it": executable file not found in $PATH
# Hence two variables rather than one with the flag tacked on: the flag belongs
# in the flags, and keeping the shared part in DOCKER_ARGS stops the two forms
# from drifting apart.
#
DOCKER_RUN     = $(DOCKER) run --rm $(DOCKER_ARGS) $(DOCKER_IMAGE)
DOCKER_RUN_TTY = $(DOCKER) run --rm -it $(DOCKER_ARGS) $(DOCKER_IMAGE)

docker-build:
	$(DOCKER) build -t $(DOCKER_IMAGE) -f $(DOCKERFILE) .

# The container equivalent of `make check`: build, budget, all four test suites.
docker-check: docker-build
	$(DOCKER_RUN) make check

docker-firmware: docker-build
	$(DOCKER_RUN) make firmware

# Interactive, so it needs a TTY and is no use from CI. DOCKER_RUN carries no
# -it precisely so the other targets stay scriptable.
docker-shell: docker-build
	$(DOCKER_RUN_TTY) bash

docker-clean:
	-$(DOCKER) rmi $(DOCKER_IMAGE)

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
