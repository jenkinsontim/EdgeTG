-- gateway.lua -- the single, unified EdgeTG Gateway.
-- It is the ONLY authoring endpoint: it writes the two-file wire artifacts
-- that mcu_executor.c consumes and gateway.py decodes.
--
--   wire_packet.bin : the topology, in either ASCII mode or 2-bit packed mode.
--   wire_values.bin : the value blob, in the exact ts_values_encode format
--                     [u32_le count] then count x [u32_le length][bytes].
--
-- The topology itself is the canonical shape string (e.g. "_/__\\").
-- Toggle USE_PACKED to compare the two transport encodings.

local USE_PACKED = false  -- toggle: false = ASCII, true = packed

-- Canonical topology string (shape only, glyphs _, /, \)...
-- In Lua source, "\\" denotes a single backslash, so "_/__\\" is the 5-symbol
-- string: _ / _ _ \
local topology = "_/__\\"

-- The values, expressed as raw byte blobs matching ts_values_encode:
-- each value is [u32_le length][bytes]. A string value is its UTF-8 bytes;
-- a float value is its 4-byte little-endian IEEE-754 representation.
local function u32le(n) return string.pack("<I4", n) end
local function float_bytes(v) return string.pack("<f", v) end

local values = {
    { data = "sensor_01", len = 9 },          -- string value
    { data = float_bytes(25.3), len = 4 }     -- float value
}

-- Build the values blob exactly like ts_values_encode:
--   [u32_le count] then count x [u32_le length][bytes]
local function ts_values_encode_blob(vals)
    local blob = u32le(#vals)
    for _, v in ipairs(vals) do
        blob = blob .. u32le(v.len) .. v.data
    end
    return blob
end

-- 2-bit packing, matching ts_packed.c exactly: _ = 0, / = 1, \ = 2.
-- Four symbols per byte, lowest two bits hold the first symbol in the group.
local GLYPH_TO_CODE = { ["_"] = 0, ["/"] = 1, ["\\"] = 2 }
local function pack_topology(topo)
    local bytes = {}
    for i = 1, #topo, 4 do
        local b = 0
        for j = 0, 3 do
            local ch = topo:sub(i + j, i + j)
            if ch ~= "" then
                local code = GLYPH_TO_CODE[ch]
                b = b | (code << (j * 2))
            end
        end
        bytes[#bytes + 1] = string.char(b)
    end
    return table.concat(bytes)
end

-- Build the wire packet in EITHER mode, matching mcu_executor's dispatcher:
--   ASCII:  [0x00][topology ASCII bytes]
--   PACKED: [0x01][symbol count u8][packed 2-bit bytes]
local FORMAT_ASCII, FORMAT_PACKED = 0x00, 0x01
local function build_wire_packet(topo, use_packed)
    if use_packed then
        if #topo > 255 then error("topology too large for 1-byte symbol count") end
        return string.char(FORMAT_PACKED) .. string.char(#topo) .. pack_topology(topo)
    else
        return string.char(FORMAT_ASCII) .. topo
    end
end

-- === Assemble and write the two unified wire artifacts ===
local values_blob = ts_values_encode_blob(values)
local wire_packet = build_wire_packet(topology, USE_PACKED)

local fp = io.open("wire_packet.bin", "wb")
fp:write(wire_packet)
fp:close()

local fv = io.open("wire_values.bin", "wb")
fv:write(values_blob)
fv:close()

-- Summary of what was written
print("EdgeTG Gateway (unified)")
print("  mode        : " .. (USE_PACKED and "PACKED" or "ASCII"))
print("  topology    : " .. topology .. "  (" .. #topology .. " symbols)")
print("  values      : " .. #values .. " value(s)")
print("  wire_packet : " .. #wire_packet .. " bytes -> wire_packet.bin")
print("  wire_values : " .. #values_blob .. " bytes -> wire_values.bin")