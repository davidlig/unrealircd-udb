"""Independent HEL 4 binary-row/radix checksum reference (no production calls)."""
import base64
import hashlib
import struct
from urllib.parse import unquote_to_bytes


def encode_component(value):
    return ''.join(f'%{b:02X}' if b in (58, 37) or b <= 32 or b >= 127 else chr(b)
                   for b in value.encode('utf-8'))


def wire_value(value):
    if value is None:
        return '!n'
    if type(value) is int:
        if not 0 <= value <= 2**64 - 1:
            raise ValueError('uint64 out of range')
        return f'!u{value}'
    if isinstance(value, str):
        return '!s' + base64.b64encode(value.encode('utf-8')).decode('ascii')
    raise TypeError('value must be None, uint64, or string')


def components(block, path):
    parts = [unquote_to_bytes(p) for p in path.split('::')]
    if not parts or any(not p for p in parts):
        raise ValueError('empty component')
    return tuple(p if block == 'K' and i == 1 and parts[0].lower() == b'f' else p.lower()
                 for i, p in enumerate(parts))


def row_encode(block, parts, value):
    kind = 0 if value is None else 2 if type(value) is int else 1
    data = b'' if value is None else struct.pack('<Q', value) if kind == 2 else value.encode('utf-8')
    body = b''.join(struct.pack('<I', len(p)) + p for p in parts) + data
    return bytes([ord(block), len(parts), kind, 0]) + struct.pack('<II', 16 + len(body), len(data)) + bytes(4) + body


def key_hash(domain, block, parts):
    return hashlib.sha256(domain + b'\0' + block.encode('ascii') + struct.pack('<I', len(parts)) +
                          b''.join(struct.pack('<I', len(p)) + p for p in parts)).digest()


def logical_digest(records, block):
    rows = [[] for _ in range(4096)]
    for path, value in records.items():
        parts = components(block, path)
        depth = 2 if block == 'K' else 1
        entity = key_hash(b'UDB4/entity', block, parts[:depth])
        partition = (entity[0] << 4) | (entity[1] >> 4)
        rows[partition].append((parts, key_hash(b'UDB4/key', block, parts), row_encode(block, parts, value)))

    def leaf(partition, data, depth=0, prefix=0):
        count = len(data)
        weight = sum(len(row) + 128 for _, _, row in data)
        pos = block.encode('ascii') + struct.pack('<HH', partition, depth) + prefix.to_bytes(32, 'big')
        totals = struct.pack('<QQ', count, weight)
        if weight <= 16 * 1024 * 1024:
            ordered = sorted(data)
            if len({parts for parts, _, _ in ordered}) != count:
                raise ValueError('duplicate canonical key')
            content = b''.join(struct.pack('<I', len(row)) + row for _, _, row in ordered)
            digest = hashlib.sha256(b'UDB4/leaf\0' + pos + content + totals).digest()
        else:
            if depth == 256:
                raise ValueError('hash collision over leaf bound')
            bit = 1 << (255 - depth)
            left = [r for r in data if not int.from_bytes(r[1], 'big') & bit]
            right = [r for r in data if int.from_bytes(r[1], 'big') & bit]
            children = leaf(partition, left, depth + 1, prefix) + leaf(partition, right, depth + 1, prefix | bit)
            digest = hashlib.sha256(b'UDB4/branch\0' + pos + children + totals).digest()
        return digest + totals

    result = bytearray(b'UDB4/block\0' + block.encode('ascii'))
    count = weight = 0
    for partition, data in enumerate(rows):
        summary = leaf(partition, data)
        c, w = struct.unpack('<QQ', summary[32:])
        count += c; weight += w
        result.extend(struct.pack('<HQQ', partition, c, w) + summary[:32])
    result.extend(struct.pack('<QQ', count, weight))
    return hashlib.sha256(result).hexdigest()
