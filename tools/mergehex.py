#!/usr/bin/env python3
"""Merge Intel HEX files (later files win on overlap). Usage: mergehex.py out.hex in1.hex [in2.hex ...]"""
import sys


def read_hex(path, mem):
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, rtype, data = b[0], (b[1] << 8) | b[2], b[3], b[4:4 + b[0]]
        if rtype == 0:
            for i, x in enumerate(data):
                mem[base + addr + i] = x
        elif rtype == 2:
            base = ((data[0] << 8) | data[1]) << 4
        elif rtype == 4:
            base = ((data[0] << 8) | data[1]) << 16


def record(rtype, addr, data):
    b = bytes([len(data), (addr >> 8) & 0xff, addr & 0xff, rtype]) + bytes(data)
    return ':' + b.hex().upper() + '%02X' % ((-sum(b)) & 0xff)


def write_hex(path, mem):
    out = []
    upper = None
    addrs = sorted(mem)
    i = 0
    while i < len(addrs):
        start = addrs[i]
        chunk = [mem[start]]
        while (i + 1 < len(addrs) and addrs[i + 1] == addrs[i] + 1 and len(chunk) < 16
               and (addrs[i + 1] & 0xffff) != 0):
            i += 1
            chunk.append(mem[addrs[i]])
        if start >> 16 != upper:
            upper = start >> 16
            out.append(record(4, 0, [(upper >> 8) & 0xff, upper & 0xff]))
        out.append(record(0, start & 0xffff, chunk))
        i += 1
    out.append(':00000001FF')
    open(path, 'w').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mem = {}
    for p in sys.argv[2:]:
        read_hex(p, mem)
    write_hex(sys.argv[1], mem)
