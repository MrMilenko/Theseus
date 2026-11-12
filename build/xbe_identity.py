#!/usr/bin/env python3
"""Give a built XBE the certificate identity of a retail dashboard XBE.

The online library logs on as the running XBE's certificate, and T:\\ / U:\\
follow its title ID, so a replacement dashboard has to present the same
identity as the one it replaces. This copies every identity field from the
retail XBE you point it at into the target, in place. No keys live here;
they're read from your own retail XBE at build time. Keys are never printed.
"""

import argparse
import struct
import sys

# Certificate field offsets (see cxbe's Xbe.h).
FIELDS = [
    ("title id", 0x008, 4),
    ("title name", 0x00C, 0x50),
    ("alternate title ids", 0x05C, 0x40),
    ("allowed media", 0x09C, 4),
    ("game region", 0x0A0, 4),
    ("game ratings", 0x0A4, 4),
    ("disk number", 0x0A8, 4),
    ("version", 0x0AC, 4),
    ("lan key", 0x0B0, 16),
    ("signature key", 0x0C0, 16),
    ("alternate signature keys", 0x0D0, 0x100),
]
CERT_MIN = 0x1D0


def cert_offset(buf, what):
    if buf[:4] != b"XBEH":
        sys.exit(f"{what}: not an XBE")
    base, = struct.unpack_from("<I", buf, 0x104)
    cert, = struct.unpack_from("<I", buf, 0x118)
    off = cert - base
    size, = struct.unpack_from("<I", buf, off)
    if size < CERT_MIN:
        sys.exit(f"{what}: certificate too small (0x{size:x})")
    return off


def describe(buf, off):
    tid, = struct.unpack_from("<I", buf, off + 0x008)
    ver, = struct.unpack_from("<I", buf, off + 0x0AC)
    name = buf[off + 0x00C:off + 0x05C].decode("utf-16-le").split("\0")[0]
    return f"title {tid:08X} '{name}' version 0x{ver:08X}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--from", dest="src", required=True, help="retail dashboard XBE")
    ap.add_argument("--to", dest="dst", required=True, help="built XBE, patched in place")
    a = ap.parse_args()

    src = open(a.src, "rb").read()
    dst = bytearray(open(a.dst, "rb").read())
    so = cert_offset(src, a.src)
    do = cert_offset(dst, a.dst)

    before = describe(dst, do)
    for _, off, n in FIELDS:
        dst[do + off:do + off + n] = src[so + off:so + off + n]
    open(a.dst, "wb").write(dst)

    print(f"xbe_identity: {before} -> {describe(dst, do)}, keys copied")


if __name__ == "__main__":
    main()
