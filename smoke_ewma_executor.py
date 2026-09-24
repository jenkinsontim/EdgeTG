"""
smoke_ewma_executor.py
Creates wire fixtures for the EWMA-leaf smoke test:
  - wire_packet.bin: ASCII-mode topology _/__\ (root with 2 leaves)
  - wire_values.bin: 3 values (preorder 0=root, 1=leaf-A, 2=leaf-B)
      pos 0 (root): 4-byte float 25.3  (calibration target)
      pos 1 (leaf-A): 8 binary feature bytes — predict-only
      pos 2 (leaf-B): 9 bytes (8 features + label byte) — predict+update
"""
import struct

# --- topology: _/__\ in ASCII wire format (format byte 0x00)
topo = b"_/__\\"
wire_packet = bytes([0x00]) + topo

# --- values blob via ts_values_encode format:
#   [u32-le count] then count * [u32-le len] [bytes]
def encode_values(vals):
    out = struct.pack("<I", len(vals))
    for v in vals:
        out += struct.pack("<I", len(v)) + v
    return out

float_val = struct.pack("<f", 25.3)          # pos 0: root float
features_A = bytes([1, 0, 1, 1, 0, 1, 0, 1])# pos 1: leaf-A, predict-only (len==8)
features_B = bytes([0, 1, 0, 0, 1, 0, 1, 0, # pos 2: leaf-B, 8 features
                    1])                       #        + label byte 1 (len==9)

wire_values = encode_values([float_val, features_A, features_B])

with open("wire_packet.bin", "wb") as f: f.write(wire_packet)
with open("wire_values.bin", "wb") as f: f.write(wire_values)
print(f"wire_packet.bin: {len(wire_packet)} bytes  topology={topo}")
print(f"wire_values.bin: {len(wire_values)} bytes  (float + 8-feat + 9-feat+label)")
