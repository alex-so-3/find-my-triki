#!/usr/bin/env python3
"""OTA update of a find-my-triki tag from the command line (Nordic Secure DFU over BLE, bleak).

1. finds the running tag by its Apple beacon (-K file.keys) or takes --bootloader to skip,
2. asks it to restart into the bootloader (standard Buttonless DFU, 0xFE59),
3. finds the bootloader ("TrikiTagDFU") and sends the signed package.
"""
import argparse
import asyncio
import json
import struct
import sys
import zipfile
import zlib

import triki_ble
from bleak import BleakClient, BleakScanner

BUTTONLESS = '8ec90003-f315-4f60-9fb8-838830daea50'
CP = '8ec90001-f315-4f60-9fb8-838830daea50'
PKT = '8ec90002-f315-4f60-9fb8-838830daea50'
PRN = 8


async def enter_bootloader(keys, timeout, connect_timeout):
    dev = await triki_ble.find_tag(keys, timeout)
    print(f'Connecting (waits for a connectable frame, up to {connect_timeout:.0f} s)...')
    got = asyncio.Event()
    async with BleakClient(dev, timeout=connect_timeout) as c:
        print('Connected, asking the tag to restart into the bootloader')
        await c.start_notify(BUTTONLESS, lambda _, d: got.set())
        await c.write_gatt_char(BUTTONLESS, b'\x01', response=True)
        try:
            await asyncio.wait_for(got.wait(), 5)
        except asyncio.TimeoutError:
            pass
    await asyncio.sleep(1)


class Dfu:
    def __init__(self, client):
        self.c = client
        self.q = asyncio.Queue()

    def _on(self, _, data):
        self.q.put_nowait(bytes(data))

    async def _resp(self, opcode):
        while True:
            r = await asyncio.wait_for(self.q.get(), 20)
            if r[0] == 0x60 and r[1] == opcode:
                if r[2] != 0x01:
                    raise RuntimeError(f'opcode {opcode:#x} failed: result {r[2]:#x} {r[3:].hex()}')
                return r[3:]

    async def cmd(self, data):
        await self.c.write_gatt_char(CP, data, response=True)
        return await self._resp(data[0])

    async def send(self, data, sent_before, total):
        mtu = self.c.services.get_characteristic(PKT).max_write_without_response_size
        n = 0
        for i in range(0, len(data), mtu):
            await self.c.write_gatt_char(PKT, data[i:i + mtu], response=False)
            n += 1
            if n % PRN == 0:
                await self._resp(0x03)   # packet receipt notification
            done = sent_before + min(i + mtu, len(data))
            print(f'\r  {done}/{total} B ({100 * done // total}%)', end='', flush=True)

    async def obj(self, kind, data, whole, base):
        await self.cmd(bytes([0x01, kind]) + struct.pack('<I', len(data)))
        await self.send(data, base, len(whole) if kind == 2 else len(data))
        off, crc = struct.unpack('<II', await self.cmd(b'\x03'))
        expect = whole[:base + len(data)]
        if off != len(expect) or crc != zlib.crc32(expect):
            raise RuntimeError(f'CRC mismatch at {off}')

    async def run(self, init, fw):
        await self.c.start_notify(CP, self._on)
        await self.cmd(bytes([0x02]) + struct.pack('<H', PRN))
        await self.cmd(b'\x06\x01')
        print('Init packet')
        await self.obj(1, init, init, 0)
        await self.cmd(b'\x04')
        max_size = struct.unpack('<III', await self.cmd(b'\x06\x02'))[0]
        print(f'Firmware, {len(fw)} B')
        for base in range(0, len(fw), max_size):
            await self.obj(2, fw[base:base + max_size], fw, base)
            last = base + max_size >= len(fw)
            try:
                await self.cmd(b'\x04')
            except Exception:
                if not last:
                    raise   # the last execute may be cut short by the reset
        print('\nDone: the tag validates the image and restarts into it.')


async def main(a):
    z = zipfile.ZipFile(a.package)
    app = json.loads(z.read('manifest.json'))['manifest']['application']
    init, fw = z.read(app['dat_file']), z.read(app['bin_file'])
    if not a.bootloader:
        if a.rescan:
            triki_ble.forget(a.keys)
        await enter_bootloader(a.keys, a.timeout, a.connect_timeout)
    print(f'Scanning for "{a.name}"...')
    dev = await BleakScanner.find_device_by_name(a.name, timeout=30)
    if not dev:
        sys.exit('Bootloader not found')
    print(f'Found bootloader {dev.address}')
    async with BleakClient(dev, timeout=20) as c:
        await Dfu(c).run(init, fw)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('package', help='signed DFU zip (make dfu)')
    p.add_argument('-K', '--keys', help='.keys file of the tag, to find it by its Apple beacon')
    p.add_argument('--bootloader', action='store_true', help='the tag is already in the bootloader')
    p.add_argument('--name', default='TrikiTagDFU')
    p.add_argument('--timeout', type=float, default=90, help='scan timeout, s')
    p.add_argument('--connect-timeout', type=float, default=90,
                   help='how long to wait for a connectable frame of the running tag, s')
    p.add_argument('--rescan', action='store_true', help='forget the remembered tag and scan again')
    a = p.parse_args()
    if not a.bootloader and not a.keys:
        p.error('-K is needed unless --bootloader')
    asyncio.run(main(a))
