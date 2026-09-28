"""Finding a find-my-triki tag over BLE, shared by triki_config.py and triki_dfu.py.

While moving, only every 4th Apple frame of the tag accepts connections (every ~20 s while
still), so a connection has to wait for one. On macOS the tag's CoreBluetooth identifier is
remembered after the first scan, and later runs hand it straight to the system, which then
connects on the next connectable frame - no scan needed.
"""
import base64
import json
import os
import sys

from bleak import BleakScanner

CACHE = os.path.expanduser('~/.cache/find-my-triki/devices.json')


def adv_key_part(keys_path):
    """Bytes 6..11 of the advertisement key: what the tag's Apple frame carries."""
    for line in open(keys_path):
        if line.startswith('Advertisement key:'):
            return base64.b64decode(line.split(':', 1)[1].strip())[6:12]
    sys.exit(f'{keys_path}: no "Advertisement key" line')


def _load():
    try:
        with open(CACHE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _remember(key, identifier):
    ids = _load()
    ids[key] = identifier
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, 'w') as f:
        json.dump(ids, f, indent=1)


def forget(keys_path):
    ids = _load()
    if ids.pop(adv_key_part(keys_path).hex(), None):
        with open(CACHE, 'w') as f:
            json.dump(ids, f, indent=1)


async def known_device(identifier):
    """A BLEDevice for a CoreBluetooth identifier seen before, without scanning (macOS only)."""
    if sys.platform != 'darwin':
        return None
    from Foundation import NSUUID
    from bleak.backends.corebluetooth.CentralManagerDelegate import CentralManagerDelegate
    from bleak.backends.device import BLEDevice

    manager = CentralManagerDelegate()
    await manager.wait_until_ready()
    uuid = NSUUID.alloc().initWithUUIDString_(identifier)
    if uuid is None:
        return None
    found = manager.central_manager.retrievePeripheralsWithIdentifiers_([uuid])
    if not found:
        return None
    return BLEDevice(identifier, None, (found[0], manager))


async def find_tag(keys_path, scan_timeout):
    """The tag with this .keys file: from the cache if possible, otherwise by scanning."""
    key = adv_key_part(keys_path).hex()
    identifier = _load().get(key)
    if identifier:
        dev = await known_device(identifier)
        if dev:
            print(f'Using the remembered tag {identifier} (no scan)')
            return dev
    part = bytes.fromhex(key)

    def match(dev, adv):
        data = adv.manufacturer_data.get(0x004C, b'')
        return data[:2] == b'\x12\x19' and data[3:9] == part

    print('Scanning for the tag beacon (move the tag to make it faster)...')
    dev = await BleakScanner.find_device_by_filter(match, timeout=scan_timeout)
    if not dev:
        sys.exit('Tag not found')
    print(f'Found {dev.address}')
    if sys.platform == 'darwin':
        _remember(key, dev.address)
    return dev
