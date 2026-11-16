"""Extract an ext2/3/4 partition image to a directory using fwtool's ext2 parser.

Usage:
    FWTOOL=/path/to/fwtool.py python scripts/porting/extract_ext4.py <image> <outdir>

The Sony partition images carry a 1024-byte boot record before the ext superblock,
so fwtool's standard `unpack` rejects them standalone; this reads them directly.
"""
import os
import sys
from stat import S_ISDIR, S_ISLNK, S_ISREG

FWTOOL = os.environ.get('FWTOOL', 'fwtool.py')
sys.path.insert(0, FWTOOL)

from fwtool.archive import ext2  # noqa: E402


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    f = open(src, 'rb')
    count = 0
    skipped = []
    for item in ext2.readExt2(f):
        target = os.path.join(dst, item.path.lstrip('/').replace('/', os.sep))
        if S_ISDIR(item.mode):
            os.makedirs(target, exist_ok=True)
            continue
        if not S_ISREG(item.mode) and not S_ISLNK(item.mode):
            skipped.append(item.path)  # device nodes: cannot materialize on host
            continue
        try:
            os.makedirs(os.path.dirname(target) or '.', exist_ok=True)
            with open(target, 'wb') as out:
                out.write(item.contents.read())
            count += 1
        except Exception as e:
            skipped.append('%s (%s)' % (item.path, e))
    print('extracted %d files to %s; skipped %d entries' % (count, dst, len(skipped)))


if __name__ == '__main__':
    main()
