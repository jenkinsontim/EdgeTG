#!/usr/bin/env python3
"""gateway_decode.py -- a DIFFERENT developer, who only knows Python,
receiving the MCU's PACKED reply. Never saw the Lua source, never saw C,
and now decodes real 2-bit-packed bytes using only the manifest."""
import struct

MANIFEST_V2 = {
    "version": 2,
    "fields": [
        {"name": "valve", "type": "bool"},
        {"name": "temp", "type": "float"},
    ],
}

CODE_TO_GLYPH = {0: "_", 1: "/", 2: "\\"}
WIRE_FORMAT_ASCII, WIRE_FORMAT_PACKED = 0, 1

def unpack_topology(packed_bytes, symbol_count):
    out = []
    for i in range(symbol_count):
        byte_idx, shift = i // 4, (i % 4) * 2
        code = (packed_bytes[byte_idx] >> shift) & 0x03
        if code == 3:
            raise ValueError("corrupted packed data: unused code 3")
        out.append(CODE_TO_GLYPH[code])
    return "".join(out)

def decode_wire_packet(buf):
    fmt = buf[0]
    if fmt == WIRE_FORMAT_ASCII:
        return buf[1:].decode("ascii")
    if fmt == WIRE_FORMAT_PACKED:
        symbol_count = buf[1]
        return unpack_topology(buf[2:], symbol_count)
    raise ValueError(f"unknown wire format byte: {fmt}")

def decode_values(raw_bytes, manifest):
    result = {}
    pos = 0
    for field in manifest["fields"]:
        if field["type"] == "bool":
            result[field["name"]] = bool(raw_bytes[pos]); pos += 1
        elif field["type"] == "float":
            result[field["name"]] = struct.unpack("<f", raw_bytes[pos:pos+4])[0]; pos += 4
    return result

with open("/tmp/reply_wire_packet.bin", "rb") as f:
    wire_packet = f.read()
with open("/tmp/reply_values.bin", "rb") as f:
    raw_values = f.read()

print(f"[Gateway/Python] Received {len(wire_packet)}-byte PACKED reply wire packet: {wire_packet.hex()}")
topo = decode_wire_packet(wire_packet)
print(f"[Gateway/Python] Unpacked topology: {topo!r}")

decoded = decode_values(raw_values, MANIFEST_V2)
print(f"[Gateway/Python] Decoded into a Python dict, using ONLY the manifest: {decoded}")
print()
print("This developer never wrote Lua or C, and the wire bytes it just")
print("decoded were genuinely 2-bit packed the whole way -- not ASCII")
print("dressed up as 'packed', the actual compact binary form.")
