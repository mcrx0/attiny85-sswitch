# Build and test environment for attiny85-sswitch.
#
# This is a TOOLBOX, not a builder: it copies none of the source. You bind-mount
# the working tree into the running container and run make targets against it.
#   docker build -t attiny85-sswitch-build -f docker/Dockerfile .
#   make docker-check
# See docker/README.md for the full guide.
#
# Why this exists rather than an off-the-shelf AVR image: `make check` is not
# just a cross-compile. It also compiles the firmware sources a second time as
# *host* code (needs a host cc), walks USB descriptors out of the ELF (needs
# python3), and drives tools/sswitch against a pty test double (needs pyserial).
# AVR-only images have none of those, so they can build the firmware and cannot
# tell you whether it is correct.
#
# Why ubuntu:24.04 specifically: it ships byte-identical AVR tooling to what
# this firmware was developed against -- gcc-avr 1:7.3.0+Atmel3.7.0-1,
# binutils-avr 2.26.20160125+Atmel3.7.0-2, avr-libc 1:2.0.0+Atmel3.7.0-1 -- so
# the container produces the same binary as the bare-metal bootstrap does. The
# same firmware under avr-gcc 14.1.0 (what thucon/avr-gcc ships) is 5002 bytes
# instead of 5032: still correct, but no longer the thing the flash budget and
# the docs were written against.

# Pinned by digest, not by tag. ubuntu:24.04 is a moving tag; this is the exact
# image manifest it resolved to. To refresh deliberately:
#   docker pull ubuntu:24.04
#   docker image inspect ubuntu:24.04 --format '{{index .RepoDigests 0}}'
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

ENV DEBIAN_FRONTEND=noninteractive

#
# The three AVR packages are version-pinned to match .toolchain/debs/ exactly.
# That is the whole point of choosing this base: a container that silently
# upgrades avr-gcc is a container whose flash numbers stop meaning anything.
# If an apt build ever fails with "Version ... not found", Ubuntu has moved the
# revision -- check `apt-cache policy gcc-avr` inside a fresh container and
# update all three here together.
#
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc-avr=1:7.3.0+Atmel3.7.0-1 \
        binutils-avr=2.26.20160125+Atmel3.7.0-2 \
        avr-libc=1:2.0.0+Atmel3.7.0-1 \
        make \
        gcc \
        libc6-dev \
        python3 \
        python3-serial \
 && rm -rf /var/lib/apt/lists/*

#
# Smoke test, mirroring the one in scripts/bootstrap-toolchain.sh. A base image
# where avr-gcc cannot actually produce an ATtiny85 object is worse than no
# image, and the failure otherwise surfaces as a confusing link error three
# targets later.
#
RUN printf 'int main(void){for(;;);}\n' > /tmp/smoke.c \
 && avr-gcc -mmcu=attiny85 -Os -o /tmp/smoke.elf /tmp/smoke.c \
 && avr-size --format=avr --mcu=attiny85 /tmp/smoke.elf \
 && rm -f /tmp/smoke.c /tmp/smoke.elf

#
# avrdude is deliberately NOT installed. It is only useful with an ISP
# programmer, which is not how a Digispark is normally flashed; adding it would
# pull in libusb and imply that `make docker-flash` works without USB
# passthrough. See "Flashing from the container" in docker/README.md.
#

# Interactive default. The Makefile targets override this with your host uid so
# that build output lands in the bind-mounted tree owned by you -- see the note
# on --user in docker/README.md.
RUN useradd --create-home --shell /bin/bash builder
USER builder
WORKDIR /src

# `docker run --rm -v "$PWD:/src" attiny85-sswitch-build` with no command.
CMD ["make", "check"]