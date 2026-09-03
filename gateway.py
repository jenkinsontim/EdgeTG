import struct

print("Gateway: Waiting for MCU reply...")
with open("wire_reply.bin", "rb") as f:
    version = struct.unpack("B", f.read(1))[0]
    if version != 2:
        print(f"ERROR: Version mismatch {version}")
        exit(1)

    topo_len = struct.unpack("B", f.read(1))[0]
    topo = f.read(topo_len).decode('utf-8')

    blob = f.read()

    # Decode values (ts_values_decode-compatible format):
    # [u32 little-endian count] then count x [u32 little-endian length][bytes]
    pos = 0
    count = struct.unpack("<I", blob[pos:pos+4])[0]
    pos += 4
    values = []
    for i in range(count):
        ln = struct.unpack("<I", blob[pos:pos+4])[0]
        pos += 4
        values.append(blob[pos:pos+ln])
        pos += ln
    sensor_id = values[0].decode('utf-8')
    temp = struct.unpack("<f", values[1])[0]

    print(f"Gateway: Received reply from MCU.")
    print(f"Dict: {{ 'sensor_id': '{sensor_id}', 'temp': {temp} }}")