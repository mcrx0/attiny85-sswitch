# Building

Two ways to build this firmware, both producing **the same bytes**. Pick
whichever suits the machine you are on.

| | [Native](#native) | [Docker](#docker) |
|---|---|---|
| Setup | `scripts/bootstrap-toolchain.sh` | `make docker-build` |
| Needs root | no | no |
| Touches the system | no, all in `./.toolchain/` | no, all in the image |
| Needs network | once, at bootstrap | once, at image build |
| Works on a locked-down machine | yes | needs a working Docker daemon |
| CI-friendly | needs apt on the runner | one `make docker-check` |
| avr-gcc version | 7.3.0 | 7.3.0 |
| Output | 5032 B flash, 308 B RAM | **identical**, byte for byte |

Both paths give you the same `avr-gcc`, so the flash budget in
[`docs/toolchain.md`](../docs/toolchain.md) means the same thing either way.
That is verified, not assumed — see [Parity](#parity).

## Docker

```bash
make docker-check      # build the image, then build and test the firmware in it
```

That is the whole thing. First run takes a few minutes to pull the base image
and install packages; after that Docker caches the layers and it is seconds.

Other targets:

```bash
make docker-build      # build the image only
make docker-firmware   # firmware only, writes .hex/.bin into firmware/build/
make docker-shell      # interactive shell in the container
make docker-clean      # remove the image
```

`docker-check` runs the same four suites as `make check`: 49 protocol checks,
45 descriptor checks, the vendor guard, and 32 CLI tests.

## Native

```bash
scripts/bootstrap-toolchain.sh   # unpacks the AVR toolchain into ./.toolchain/
make check                       # or: make firmware
```

No root, no `apt install`, nothing outside this directory. `.toolchain/` is
gitignored and safe to delete. Full details in
[`docs/toolchain.md`](../docs/toolchain.md).

Already have `avr-gcc` on `PATH`? Skip the bootstrap entirely.

## How the image works

It is a **toolbox, not a builder**. The Dockerfile copies none of the source:

```
docker run --rm -v "$PWD:/src" -w /src attiny85-sswitch-build make check
```

Your working tree is bind-mounted in and make runs against it directly. Two
consequences worth knowing:

- **Editing a `.c` file needs no rebuild.** There is no copy of the source
  inside the image to go stale.
- **Build output is written to your real tree**, not to a layer you then have
  to extract. `firmware/build/attiny85-sswitch.hex` is right there afterwards,
  ready for `make flash`.

`make` passes `--user "$(id -u):$(id -g)"`. The image has a `builder` user at
uid 1000, but if the container wrote build products as a uid that is not yours
on the host, every file in `firmware/build/` would belong to somebody else and
cleaning up would need `sudo`. Passing your host uid keeps ownership correct on
any machine, whatever uid you happen to have. `HOME` is redirected to `/tmp` for
the same reason: your uid has no passwd entry inside the image.

## Parity

Same firmware, same toolchain, same output:

```
container:  8b697d0c93d191e11a21a0be936340c1cf930b1edb83bb5c1f0d86068197f4c2  attiny85-sswitch.hex
host:       8b697d0c93d191e11a21a0be936340c1cf930b1edb83bb5c1f0d86068197f4c2  attiny85-sswitch.hex
```

This is why the image pins its packages rather than floating on `latest`:

| Package | Pinned version |
|---|---|
| `gcc-avr` | `1:7.3.0+Atmel3.7.0-1` |
| `binutils-avr` | `2.26.20160125+Atmel3.7.0-2` |
| `avr-libc` | `1:2.0.0+Atmel3.7.0-1` |

To check it yourself:

```bash
sha256sum firmware/build/attiny85-sswitch.hex     # after make docker-firmware
sha256sum firmware/build/attiny85-sswitch.hex     # after make firmware
```

## Reproducibility

Two pins, both deliberate:

- **Base image by digest** — `ubuntu:24.04@sha256:534bea…`. `ubuntu:24.04` is a
  moving tag; the digest is the exact manifest it resolved to when this was
  written.
- **AVR packages by version** — so an Ubuntu security update cannot silently
  change what your firmware compiles to.

If a build ever fails with `Version '1:7.3.0+Atmel3.7.0-1' for 'gcc-avr' was not
found`, Ubuntu has moved the revision. Find the current one and update all
three pins in `docker/Dockerfile` together:

```bash
docker run --rm ubuntu:24.04 sh -c \
  'apt-get update -qq; apt-cache policy gcc-avr binutils-avr avr-libc'
```

To refresh the base image digest:

```bash
docker pull ubuntu:24.04
docker image inspect ubuntu:24.04 --format '{{index .RepoDigests 0}}'
```

The build does need network, once, for `apt-get`. After that the image is
self-contained. `apt-get install` against Ubuntu is the one part that is not
hermetic; if that matters, `scripts/bootstrap-toolchain.sh` already downloads
pinned `.deb` files you could `COPY` in instead.

## Flashing from the container

**Not provided, and not tested.** `avrdude` and `micronucleus` are deliberately
absent from the image: `avrdude` needs an ISP programmer rather than the normal
USB bootloader path, and both need real USB access.

To try it anyway, pass the USB bus in and install a flasher:

```bash
docker run --rm -it --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v /dev/bus/usb -v "$PWD:/src" -w /src \
  --entrypoint sh attiny85-sswitch-build -c \
  'apt-get update -qq && apt-get install -y avrdude && make flash'
```

Treat that as untested until you have tried it on a board. Flashing is
hardware work and nothing in this repository has been run against a Digispark —
see [`tests/README.md`](../tests/README.md) for the checks that still need
doing.

## CI

```yaml
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: make docker-check
```

One step. No toolchain installation, no matrix of avr-gcc versions, and the
runner needs nothing preinstalled beyond Docker.

## Troubleshooting

**`docker: error getting credentials - err: exit status 2`**

A broken credential helper in `~/.docker/config.json`, e.g. a Windows
`"credsStore": "desktop.exe"` on a Linux host. Docker tries to exec it before
pulling, and fails. Bypass it per-invocation:

```bash
mkdir -p /tmp/dockercfg && echo '{"auths":{}}' > /tmp/dockercfg/config.json
DOCKER_CONFIG=/tmp/dockercfg make docker-check
```

**`make docker-build` sends 160 MB and takes forever**

`.dockerignore` is missing or has been edited. It keeps `.toolchain/` and
`.git` out of the build context; without it Docker tars up the whole unpacked
toolchain on every build. `ls -a` should show it at the repo root.

**`Permission denied` writing into `firmware/build/`**

You are probably running the raw `docker run` from this file instead of the
make target, which is missing `--user`. Add it, or just use `make docker-*`.

**`avr-gcc: not found`**

The image is stale or was never built. `make docker-check` depends on
`docker-build`, so it should not happen; if it does, `make docker-clean` then
`make docker-build`.

## Alternatives, and why not

Both were tested against this repository before the image was written.

**`balena/avr-gcc`** — no longer exists. The pull fails with `pull access
denied`; Balena dropped or renamed it.

**`thucon/avr-gcc`** — real, pulls fine, and the firmware *does* build on it,
compiling clean under `avr-gcc 14.1.0` at 5002 bytes. It cannot run
`make check`:

- no `pyserial`, so the 32 CLI tests fail
- installing it does not help. The image's bundled interpreter is Python 3.13
  at a relocated `/usr/sbin`, while `pacman` installs pyserial into
  `/usr/lib/python3.14/site-packages` — the package and the interpreter do not
  meet
- it is Arch, a rolling release, so an unpinned `:latest` drifts underneath you

That last combination — an AVR-only image that cannot execute the test suite —
is the general problem. `make check` is not just a cross-compile: it recompiles
the firmware sources as *host* code, walks USB descriptors out of the ELF, and
drives the CLI against a pty test double. An image needs a host compiler and
Python, not just a cross-compiler.

## Layout

```
docker/
  Dockerfile     the image
  README.md      this file
.dockerignore    keeps .toolchain/ and .git out of the build context
```