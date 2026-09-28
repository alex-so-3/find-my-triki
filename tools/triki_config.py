#!/usr/bin/env python3
"""Configure a Triki tag over BLE.

The tag accepts connections at any time on its Apple beacon (find it with -K, the
.keys file) or in config mode (hold the button 3 s, found by name "TrikiTag").
A still tag advertises only every ~20 s: move it to connect faster.

Examples:
  triki_config.py -a abcdefgh -K keys/TRIKI.keys --ring             # blink the LED (DULT sound)
  triki_config.py -a abcdefgh -K keys/TRIKI.keys --info             # battery, settings
  triki_config.py -a abcdefgh -k ~/keys/ABC_keyfile -f 00112233...  # write keys
  triki_config.py -a abcdefgh -p 1 -d 2 -l 900 -m 80               # tune power/intervals
  triki_config.py -a abcdefgh --dfu                                 # reboot into the DFU bootloader

Requires: pip install bleak
"""
import argparse
import asyncio
import struct
import sys

import triki_ble
from bleak import BleakClient, BleakScanner

SERVICE = '5cfce313-a7e3-45c3-933d-418b8100da7f'


def chr_uuid(short):
    return f'8c5de{short}-ad8d-4810-a31f-53862e79ee77'


AUTH, KEY, FMDN_ON, APPLE_ON, PERIOD = chr_uuid('bdf'), chr_uuid('bde'), chr_uuid('bdb'), chr_uuid('bdc'), chr_uuid('bdd')
STILL, TXPOWER, FMDN_KEY, MAC = chr_uuid('be0'), chr_uuid('be1'), chr_uuid('be2'), chr_uuid('be4')
STATUS, MOTION, DFU = chr_uuid('be5'), chr_uuid('be6'), chr_uuid('be7')
INFO = chr_uuid('be9')
# Standard DULT non-owner control point: Sound_Start 0x0300 / Sound_Stop 0x0301 (little-endian)
DULT_CP = '8e0c0001-1d68-fb92-bf61-48377421680e'
GAME_MAC, GAME_NAME = chr_uuid('bea'), chr_uuid('beb')
INFO_FORMAT = '<BHHHHIBBBBBB'
INFO_FIELDS = ('layout', 'app_version', 'battery_mv', 'still_timeout_s', 'motion_threshold_mg',
               'status_flags', 'period', 'tx_power', 'apple_enabled', 'fmdn_enabled', 'moving', 'imu_ok')



def u32(v):
    return struct.pack('<I', int(v, 0) if isinstance(v, str) else v)


async def find_device(args):
    if args.address:
        return await triki_ble.known_device(args.address) or args.address
    if args.keys:
        if args.rescan:
            triki_ble.forget(args.keys)
        return await triki_ble.find_tag(args.keys, max(args.timeout, 30))
    print(f'Scanning for "{args.name}" (hold the tag button for 3 s)...')
    dev = await BleakScanner.find_device_by_name(args.name, timeout=args.timeout)
    if not dev:
        sys.exit('Tag not found')
    print(f'Found {dev.address}')
    return dev


async def main(args):
    target = await find_device(args)
    print(f'Connecting (waits for a connectable frame, up to {args.connect_timeout:.0f} s)...')
    async with BleakClient(target, timeout=args.connect_timeout) as c:
        async def w(uuid, data):
            await c.write_gatt_char(uuid, data, response=True)

        auth = args.auth.encode()
        if len(auth) != 8:
            sys.exit('Password must be exactly 8 characters')
        await w(AUTH, auth)

        if args.keyfile:
            data = open(args.keyfile, 'rb').read()
            if len(data) < 29 or data[0] < 1:
                sys.exit('keyfile has no keys')
            key = data[1:29]
            await w(KEY, key[:14])
            await w(KEY, key[14:])
            print('Apple key written')
        if args.fmdnkey:
            key = bytes.fromhex(args.fmdnkey)
            if len(key) != 20:
                sys.exit('FMDN key must be 20 bytes (40 hex characters)')
            await w(FMDN_KEY, key)
            print('FMDN key written')
        if args.fmdn is not None:
            await w(FMDN_ON, u32(args.fmdn))
        if args.airtag is not None:
            await w(APPLE_ON, u32(args.airtag))
        if args.period is not None:
            await w(PERIOD, u32(args.period))
        if args.txpower is not None:
            await w(TXPOWER, u32(args.txpower))
        if args.still is not None:
            await w(STILL, u32(args.still))
        if args.motion is not None:
            await w(MOTION, u32(args.motion))
        if args.status is not None:
            await w(STATUS, u32(int(args.status, 16)))
        if args.mac:
            # aa:bb:cc:dd:ee:ff -> little endian as stored by the tag
            await w(MAC, bytes(reversed(bytes.fromhex(args.mac.replace(':', '')))))
        if args.game_mac is not None:
            mac = args.game_mac.replace(':', '')
            if mac == '' or int(mac, 16) == 0:
                await w(GAME_MAC, bytes(6))          # all-zero disables game mode
                print('Game mode disabled')
            else:
                await w(GAME_MAC, bytes(reversed(bytes.fromhex(mac))))
                print('Game MAC set')
        if args.game_name is not None:
            name = args.game_name.encode()
            if len(name) > 20:
                sys.exit('Game name must be at most 20 bytes')
            await w(GAME_NAME, name)
            print('Game name set')
        if args.newauth:
            if len(args.newauth) != 8:
                sys.exit('New password must be exactly 8 characters')
            await w(AUTH, args.newauth.encode())
            print('Password changed')
        if args.info:
            values = struct.unpack(INFO_FORMAT, await c.read_gatt_char(INFO))
            for name, value in zip(INFO_FIELDS, values):
                print(f'  {name}: {hex(value) if name == "status_flags" else value}')
        if args.ring:
            await w(DULT_CP, bytes([0x00, 0x03]))
            print('Blinking the LED (about 10 s)')
        if args.stop_ring:
            await w(DULT_CP, bytes([0x01, 0x03]))
            print('Blinking stopped')
        if args.dfu:
            await w(DFU, b'\x01')
            print('Tag will reboot into the bootloader ("TrikiTagDFU") after disconnect')
    print('Done. Changed settings are saved and the tag restarts after the disconnect.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('-a', '--auth', required=True, help='password (8 characters, default abcdefgh)')
    p.add_argument('-i', '--address', help='BLE address/UUID (default: scan by name)')
    p.add_argument('-K', '--keys', help='find the tag by its Apple beacon (.keys file) instead of by name')
    p.add_argument('--name', default='TrikiTag')
    p.add_argument('--timeout', type=float, default=20, help='scan timeout, s')
    p.add_argument('--connect-timeout', type=float, default=90,
                   help='how long to wait for a connectable frame, s (the tag accepts connections '
                        'on every 4th Apple frame while moving, every ~20 s while still)')
    p.add_argument('--rescan', action='store_true', help='forget the remembered tag and scan again')
    p.add_argument('-k', '--keyfile', help='OpenHaystack *_keyfile, the first key is used')
    p.add_argument('-f', '--fmdnkey', help='Google FMDN EID, 40 hex characters')
    p.add_argument('-g', '--fmdn', help='FMDN broadcasting 0/1')
    p.add_argument('-t', '--airtag', help='Apple broadcasting 0/1')
    p.add_argument('-d', '--period', help='interval multiplier 1/2/4/8 (2 = one packet per network every 2 s while moving)')
    p.add_argument('-p', '--txpower', help='TX power 0 (-8 dBm), 1 (0 dBm), 2 (+4 dBm)')
    p.add_argument('-l', '--still', help='seconds without motion before slow advertising (30..7200)')
    p.add_argument('-m', '--motion', help='motion threshold in mg (0 = IMU off, always fast)')
    p.add_argument('-s', '--status', help='status byte flags, hex (default 448000)')
    p.add_argument('-c', '--mac', help='address in config mode, aa:bb:cc:dd:ee:ff')
    p.add_argument('-n', '--newauth', help='new password (8 characters)')
    p.add_argument('--ring', action='store_true', help='blink the LED for about 10 s (DULT play sound)')
    p.add_argument('--stop-ring', action='store_true', help='stop blinking')
    p.add_argument('--info', action='store_true', help='print battery, version and settings')
    p.add_argument('--game-mac', help='game-controller mode BLE address aa:bb:cc:dd:ee:ff (0 or empty = disable)')
    p.add_argument('--game-name', help='game-controller advertised name, up to 20 bytes')
    p.add_argument('--dfu', action='store_true', help='reboot into the DFU bootloader')
    asyncio.run(main(p.parse_args()))
