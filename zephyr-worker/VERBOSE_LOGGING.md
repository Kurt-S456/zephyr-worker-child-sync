Verbose logging and reproduction
=================================

What I changed
- Added verbose BAD_MAGIC diagnostics to `zephyr-worker/src/main.c` which now prints:
  - `ERROR,...,BAD_MAGIC(...)` (existing CSV-style marker)
  - `DETAILS,...` (expected vs actual magic and parsed fields)
  - `RXHEX,...` (hex dump of the raw received bytes)

Build & flash (examples)
- PlatformIO (if you use PlatformIO):

```bash
cd zephyr-worker
pio run
# flash (project depending) e.g.:
pio run -t upload
```

- West / Zephyr (if you use west): adjust board name:

```bash
cd zephyr-worker/zephyr
west build -b <board> ..
west flash
```

Capture logs
- Use `platformio device monitor` or `screen` to watch serial output. Example:

```bash
pio device monitor -p /dev/ttyUSB0 -b 115200
# or
screen /dev/ttyUSB0 115200
```

Reproduce locally with a malformed packet
- A small helper script `scripts/generate_bad_packet.py` creates a binary file `bad_packet.bin` containing
  a `slave_report`-shaped record with a zero magic (BAD_MAGIC). This is for offline inspection and to
  compare the `RXHEX` output produced by the firmware.

Files
- `zephyr-worker/src/main.c` (patched)
- `zephyr-worker/scripts/generate_bad_packet.py` (test helper)
