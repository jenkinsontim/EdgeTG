-- gateway_encode.lua -- "the developer writes Lua" side of the Gateway

-- A manifest is the ONLY thing that has to be shared between Gateway
-- instances (possibly written in different languages). It maps
-- positions to names and types. This one is version 2.
local MANIFEST_V2 = {
  version = 2,
  fields = {
    {name = "valve",  type = "bool"},
    {name = "temp",   type = "float"},
  }
}

local MANIFEST_V3 = {
  version = 3,
  fields = {
    {name = "valve",    type = "bool"},
    {name = "temp",     type = "float"},
    {name = "humidity", type = "float"},
  }
}

-- Build the topology string for N flat values under one root: _/__..._\
local function topology_for_arity(n)
  local s = "_/"
  for i = 1, n do s = s .. "_" end
  return s .. "\\"
end

-- Encode a single value into bytes matching its declared type.
local function encode_value(v, ty)
  if ty == "bool" then
    return string.char(v and 1 or 0)
  elseif ty == "float" then
    return string.pack("<f", v)
  else
    error("unknown type: " .. ty)
  end
end

-- 2-bit packing, matching ts_packed.c exactly: _ = 0, / = 1, \ = 2
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

-- Build the full wire packet in EITHER mode, matching mcu_executor's dispatcher exactly:
--   ASCII:  [0x00][topology bytes]
--   PACKED: [0x01][symbol count, 1 byte][packed topology bytes]
local FORMAT_ASCII, FORMAT_PACKED = 0, 1
local function build_wire_packet(topo, use_packed)
  if use_packed then
    if #topo > 255 then error("topology too large for 1-byte count") end
    return string.char(FORMAT_PACKED) .. string.char(#topo) .. pack_topology(topo)
  else
    return string.char(FORMAT_ASCII) .. topo
  end
end

-- The actual Gateway "compile" step: human table + manifest -> (topology, value_bytes)
local function gateway_encode(config, manifest)
  local topology = topology_for_arity(#manifest.fields)
  local values = ""
  for _, field in ipairs(manifest.fields) do
    local v = config[field.name]
    if v == nil then error("config missing field: " .. field.name) end
    values = values .. encode_value(v, field.type)
  end
  return topology, values, manifest.version
end

-- === Demo: a developer who thinks entirely in Lua tables ===
local USE_PACKED = true  -- toggle this to compare wire sizes

local config = { valve = true, temp = 25.3 }
local topo, vals, ver = gateway_encode(config, MANIFEST_V2)
local wire_packet = build_wire_packet(topo, USE_PACKED)

print("Developer wrote (Lua table): valve=true, temp=25.3")
print("Manifest version: " .. ver)
print("Topology (canonical form): " .. topo .. "  (" .. #topo .. " symbols)")
print("Wire mode: " .. (USE_PACKED and "PACKED" or "ASCII"))
print("Wire packet bytes (hex): " .. wire_packet:gsub(".", function(c) return string.format("%02x ", c:byte()) end))
print("Wire packet length: " .. #wire_packet .. " bytes (topology portion) + " .. #vals .. " (values) = " .. (#wire_packet + #vals) .. " total")

-- Write these to files so the C "MCU" process can consume them
local f = io.open("/tmp/wire_packet.bin", "wb"); f:write(wire_packet); f:close()
local f2 = io.open("/tmp/wire_values.bin", "wb"); f2:write(vals); f2:close()
local f3 = io.open("/tmp/wire_manifest_version.txt", "w"); f3:write(tostring(ver)); f3:close()
