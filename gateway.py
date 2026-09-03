"""gateway.py -- the single, unified EdgeTG reply decoder.

Reads the two reply artifacts that mcu_executor.c produces and prints the
final decoded dictionary for the developer.

  reply_packet.bin : topology, in the same transport mode the request used.
  reply_values.bin : the value blob, ts_values_encode format
                     [u32_le count] then count x [u32_le length][bytes].
"""

import struct


def unpack_topology(fmt, payload):
    """Reconstruct the ASCII topology string from a transport payload.

    ASCII  mode:   payload IS the topology bytes.
    Packed mode:   payload is [symbol_count u8] then 2-bit packed bytes
                   (_=0, /=1, \\=2), 4 symbols/byte, low bits first.
    """
    if fmt == 0x00:
        return payload.decode('utf-8'), len(payload)
    if fmt == 0x01:
        if len(payload) < 1:
            raise ValueError("packed packet missing symbol count")
        symbol_count = payload[0]
        packed = payload[1:]
        code_to_glyph = {0: '_', 1: '/', 2: '\\'}
        out = []
        for i in range(symbol_count):
            byte = packed[i // 4]
            code = (byte >> ((i % 4) * 2)) & 0x03
            if code == 3:
                raise ValueError("corrupted packed topology (code 3)")
            out.append(code_to_glyph[code])
        return ''.join(out), symbol_count
    raise ValueError("unknown wire format byte 0x%02x" % fmt)


def decode_values_blob(blob):
    """Decode [u32_le count] then count x [u32_le length][bytes]."""
    pos = 0
    count = struct.unpack_from("<I", blob, pos)[0]
    pos += 4
    values = []
    for _ in range(count):
        ln = struct.unpack_from("<I", blob, pos)[0]
        pos += 4
        values.append(blob[pos:pos + ln])
        pos += ln
    if pos != len(blob):
        raise ValueError("value blob has trailing bytes")
    return values


def main():
    with open("reply_packet.bin", "rb") as f:
        wire = f.read()

    fmt = wire[0]
    topo, _ = unpack_topology(fmt, wire[1:])
    print("Gateway: reply wire mode=%s, topology=\"%s\"" % (
        "packed" if fmt == 0x01 else "ascii", topo))

    with open("reply_values.bin", "rb") as f:
        blob = f.read()
    values = decode_values_blob(blob)

    sensor_id = values[0].decode('utf-8')
    temp = struct.unpack("<f", values[1])[0]

    print("Gateway: received reply from MCU.")
    print("Dict: { 'sensor_id': '%s', 'temp': %s }" % (sensor_id, temp))


if __name__ == "__main__":
    main()