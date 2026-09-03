-- gateway.lua
local MANIFEST_VERSION = 2

-- The human-readable config
local config = {
    topology = "_/_\\", -- Root with 2 leaves
    values = {
        { type = "string", val = "sensor_01" },
        { type = "float",  val = 25.3 } -- The original temperature
    }
}

-- Encode values to the ts_values_decode-compatible blob:
-- [u32 little-endian count] then count x [u32 little-endian length][bytes]
local blob = string.pack("<I4", 2) -- two values
local str = config.values[1].val
blob = blob .. string.pack("<I4", #str) .. str          -- value 0: length-prefixed string
blob = blob .. string.pack("<I4", 4) .. string.pack("<f", config.values[2].val) -- value 1: 4-byte float

-- Write the wire payload
local f = io.open("wire_payload.bin", "wb")
f:write(string.pack("B", MANIFEST_VERSION))
f:write(string.pack("B", #config.topology))
f:write(config.topology)
f:write(blob)
f:close()
print("Gateway: Sent config to MCU.")
