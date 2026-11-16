"""Dump a FAT directory tree (path, sizes) from a Sony partition image.

Usage:
    FWTOOL=/path/to/fwtool.py python scripts/porting/read_fat.py <image> [window_size]

Sony partitions are FAT12/FAT16 with mkfs.fat headers; fwtool's fat reader handles
them but standalone `unpack` rejects non-top-level formats, so list directly.
"""
import os
import struct
import sys

FWTOOL = os.environ.get('FWTOOL', 'fwtool.py')
sys.path.insert(0, FWTOOL)

from fwtool.archive import fat  # noqa: E402
from fwtool.io import FilePart  # noqa: E402


def main():
    src = sys.argv[1]
    window = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    size = os.path.getsize(src)
    f = FilePart(open(src, 'rb'), 0, window or size)
    files = list(fat.readFat(f))
    print('%d entries in %s:' % (len(files), src))
    for item in files:
        print('%10d  %s' % (item.size, item.path))


if __name__ == '__main__':
    main()
