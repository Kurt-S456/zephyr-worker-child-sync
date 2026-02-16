#!/usr/bin/env python3
"""Generate a malformed slave_report binary (BAD_MAGIC) for offline testing."""
import struct
from pathlib import Path

OUT = Path(__file__).parent / '..' / 'bad_packet.bin'

def main():
    # Construct a slave_report with BAD magic (0x00000000)
    magic = 0x00000000
    _id = 1
    offset = -12345
    synced = 123456789
    # Big-endian packing to emulate network/raw ordering
    payload = struct.pack('>I I q Q', magic, _id, offset, synced)
    outp = OUT.resolve()
    outp.parent.mkdir(parents=True, exist_ok=True)
    with open(outp, 'wb') as f:
        f.write(payload)
    print('Wrote', outp)
    print('Hex:', payload.hex())

if __name__ == '__main__':
    main()
