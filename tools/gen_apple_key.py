#!/usr/bin/env python3
"""Generate an OpenHaystack (Apple Find My) key pair for the tag.

Writes into keys/ (not committed):
  <name>.keys     private key, advertisement key and hashed advertisement key
                  (format used by macless-haystack and biemster/FindMy)
  <name>_keyfile  binary keyfile for the firmware (1 byte count + 28 byte key)

Keep the private key: it is the only way to decrypt the location reports.
Requires: pip install cryptography
"""
import argparse
import base64
import hashlib
import os

from cryptography.hazmat.primitives.asymmetric import ec

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('-n', '--name', default='TRIKI', help='file name prefix (default TRIKI)')
p.add_argument('-o', '--outdir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'keys'))
a = p.parse_args()

priv = ec.generate_private_key(ec.SECP224R1())
priv_bytes = priv.private_numbers().private_value.to_bytes(28, 'big')
adv_key = priv.public_key().public_numbers().x.to_bytes(28, 'big')
hashed = hashlib.sha256(adv_key).digest()

b64 = lambda b: base64.b64encode(b).decode()
os.makedirs(a.outdir, exist_ok=True)
keys_path = os.path.join(a.outdir, f'{a.name}.keys')
keyfile_path = os.path.join(a.outdir, f'{a.name}_keyfile')
for path in (keys_path, keyfile_path):
    if os.path.exists(path):
        raise SystemExit(f'{path} already exists, refusing to overwrite (use another --name)')

with open(keys_path, 'w') as f:
    f.write(f'Private key: {b64(priv_bytes)}\n')
    f.write(f'Advertisement key: {b64(adv_key)}\n')
    f.write(f'Hashed adv key: {b64(hashed)}\n')
with open(keyfile_path, 'wb') as f:
    f.write(bytes([1]) + adv_key)

mac = bytes([adv_key[0] | 0xC0]) + adv_key[1:6]
print(f'Written {os.path.normpath(keys_path)} and {os.path.normpath(keyfile_path)}')
print(f'Advertisement key: {b64(adv_key)}')
print(f'Hashed adv key:    {b64(hashed)}')
print('Tag Apple address: ' + ':'.join('%02X' % x for x in mac))
