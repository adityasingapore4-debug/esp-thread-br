#!/usr/bin/env python3
"""ADR-091 published vectors, using Python's host-only standard library."""
import hashlib

payload = bytes.fromhex('01030000 00112233445566778899aabbccddeeff 01040800 54455354 70617373776f7264')
assert len(payload) == 36
payload_hash = hashlib.sha256(payload).digest()
assert payload_hash.hex() == '6c9a98a6dcca156b5f6e4af86459f189410580527833bcaf040680cd28c77f15'
descriptor = (bytes([1]) + bytes(range(16)) + bytes([2]) + (2).to_bytes(4, 'big')
              + bytes.fromhex('fc61 0123 42 00 00') + len(payload).to_bytes(4, 'big')
              + payload_hash + bytes(16))
assert len(descriptor) == 81
assert hashlib.sha256(descriptor).hexdigest() == '82f6973d6d725a384ac9769bfdcc248e8ecd7fad7cdffcd3253b0e0f9675cb67'
print('ADR-091 SHA-256 vectors: 2 passed')
