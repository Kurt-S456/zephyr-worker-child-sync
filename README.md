# Zephyr Worker — Child Synchronization

This repository contains two Zephyr applications implementing a simple
SPI-based synchronization protocol between a single Worker (master)
and multiple Child (slave) nodes.

Wiring
- Connect common SCLK, MOSI and MISO between master and all children.
- Provide one CS (chip‑select) GPIO per child on the master to the child's
  NSS/CS input (e.g. CS0 → CHILD0 NSS, CS1 → CHILD1 NSS, ...).
- Example signals to wire:
  - Master MOSI -> Child MOSI
  - Master MISO <- Child MISO
  - Master SCLK -> Child SCLK
  - Master CS0 -> Child0 NSS
  - Master CS1 -> Child1 NSS
  - (and so on for additional children)

Build / Flash
You can build either with West/CMake (Zephyr native) or PlatformIO.

West (Zephyr CMake)

# Zephyr Worker — Child Synchronization

This repository contains two Zephyr applications implementing a simple
SPI-based synchronization protocol between a single Worker (master)
and multiple Child (slave) nodes.

## Overview

- `zephyr-worker/`: Worker (master) application
- `zephyr-child/`: Child (slave) application

## Wiring

- Connect common `SCLK`, `MOSI` and `MISO` between master and all children.
- Provide one CS (chip-select) GPIO per child on the master to the child's
  NSS/CS input (e.g. CS0 → CHILD0 NSS, CS1 → CHILD1 NSS, ...).
- Example signals to wire:
  - Master MOSI -> Child MOSI
  - Master MISO <- Child MISO
  - Master SCLK -> Child SCLK
  - Master CS0 -> Child0 NSS
  - Master CS1 -> Child1 NSS

## Build / Flash

You can build either with West/CMake (Zephyr native) or PlatformIO.

### West (Zephyr CMake)

Build worker (example):
```bash
cd zephyr-worker
west build -b <board> -s . -DWORKER_SLAVE_COUNT=1
west flash
```

Build child (note the child ID define):
```bash
cd zephyr-child
west build -b <board> -s . -DAPP_CHILD_ID=0
west flash
```

### PlatformIO

Add build flags in `platformio.ini` or pass via `build_flags`:
```
build_flags = -DAPP_CHILD_ID=0 -DWORKER_SLAVE_COUNT=1
```
Then:
```bash
pio run -e <env> -t upload
```

## Notes and flags

- `APP_CHILD_ID` (required for child): set to the child's numeric ID (0..N).
- `ENABLE_OFFSET_VALIDATION` (default `1`): enable runtime validation of
  measured offsets against a maximum acceptable threshold. Disable by
  passing `-DENABLE_OFFSET_VALIDATION=0` at build time.
- `MAX_ACCEPTABLE_OFFSET_MS` (default `10000`): maximum acceptable offset in
  milliseconds used when offset validation is enabled. Override via
  `-DMAX_ACCEPTABLE_OFFSET_MS=<ms>` when building.

## Host / PlatformIO setup (Arch Linux notes)

On Arch-based distributions, you may need to install PlatformIO UDEV helpers
to enable flashing via STLink and similar devices:

```bash
sudo pacman -S platformio-core-udev
```

If you are using the PlatformIO-provided Python virtual environment, prepare
it and ensure required Python packages are installed (example paths may vary):

```bash
~/.platformio/penv/.zephyr-4.2.1/bin/python -m ensurepip --upgrade
~/.platformio/penv/.zephyr-4.2.1/bin/python -m pip -V
~/.platformio/penv/.zephyr-4.2.1/bin/python -m pip install pykwalify
```

## Runtime behaviour

See the source files in `zephyr-worker/src` and `zephyr-child/src` for details
about the synchronization protocol and runtime debug output.

## Debugging

Use Zephyr logging and attached debuggers (OpenOCD / probe) to inspect runtime
behaviour. For quick serial logs, run:

```bash
pio device monitor -p <port> -b <baud>
```

---

The original `readme.md` at the project root is kept as a short host-setup
note; this file consolidates wiring, build, and setup instructions.
