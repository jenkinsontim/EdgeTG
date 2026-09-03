# EdgeTG (v1.2)

Canonical ordered trees as genomes for edge intelligence. **Exactly three glyphs:** `_` `/` `\`

---

## What's New in v1.2

- **Stack Overflow Fix (`path[64]`)**: Fully dynamic tree-depth-bounded path allocation across all tree mutations (`ts_mut_grow`, `ts_mut_shrink`, `ts_mut_swap`, `ts_mut_retarget`).
- **Trailing Root Leak Fix (`TS_ERR_TRAILING`)**: Explicit tree cleanup and root node deallocation in `ts_parse` on trailing characters.
- **Recursive Nested Schema Validation**: Multi-level positional arity and max-depth enforcement (`ts_schema_validate`).
- **MCU Executor & Version-Locking Safety Net**: Manifest version validation (`EXPECTED_MANIFEST_VERSION`) rejecting invalid payloads before processing.
- **Multi-Language Gateway Loop**: Cross-language authoring in Lua (`gateway.lua`), C3 (`gateway.c3`), and Zig (`gateway.zig`), positional execution in C (`mcu_executor.c`), and response decoding in Python (`gateway.py`).

## What's New in v1.3/v1.4 Packed Wire Mode

- **2-bit packed wire transport** (`ts_packed.h/.c`): stores 4 topology glyphs per byte (`_`=0, `/`=1, `\`=2; code 3 is invalid and rejected on unpack).
- **Dual wire dispatcher** (`test_dual_wire_format.c`, `mcu_firmware.c`): the same downstream topology is reached from either an ASCII-mode or a packed-mode packet. Unknown format bytes are rejected, never guessed.
- **Packed bidirectional demo** (`gateway_encode.lua` → `mcu_firmware.c` → `gateway_decode.py`): request and reply are both genuinely 2-bit packed; the firmware applies the `25.3 -> 25.7` calibration offset to prove real processing.

---

## Quick Start

```bash
make test
```

This builds and runs every verification suite under AddressSanitizer, UndefinedBehaviorSanitizer, and LeakSanitizer with `-Werror`.

---

## Multi-Language Gateway Demos

EdgeTG supports authoring configurations in any preferred language. All produce the exact same wire format and the same `25.3 -> 25.7` calibration drift when executed by the C MCU.

### Build MCU Executor
```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 ts_core.c ts_layers.c mcu_executor.c -o mcu_executor
```

### 1. Lua (Scripting)
```bash
lua gateway.lua
./mcu_executor wire_payload.bin
python gateway.py
```

### 2. C3 (Modern Systems C)
```bash
c3c compile gateway.c3 -o gateway_c3
./gateway_c3
./mcu_executor wire_payload.bin
python gateway.py
```

### 3. Zig (Comptime Reflection & Safety)
```bash
zig build-exe gateway.zig -O ReleaseFast
./gateway
./mcu_executor wire_payload.bin
python gateway.py
```

---

## Architecture & Supported Languages

| Language / Module | Role | Description |
|---|---|---|
| **Lua** (`gateway.lua`) | Gateway Author | Rapid scripting and game/config authoring |
| **C3** (`gateway.c3`) | Gateway Author | Modern C syntax with safe ergonomics |
| **Zig** (`gateway.zig`) | Gateway Author | Comptime validation and zero-cost abstractions |
| **Python** (`gateway.py`) | Gateway Decoder | Dynamic data analysis, telemetry decoding |
| **C Substrate** (`ts_*.c`, `mcu_executor.c`) | Edge MCU Executor | Ultra-compact, pure canonical tree processing |

---

## Core Invariants

1. **Three glyphs only**: No names, values, or labels in the string (`_`, `/`, `\`).
2. **Strict canonicity**: `encode ∘ parse = id`.
3. **Meaning is external**: Positional indices bind topology to data/schemas.
4. **Empty child lists illegal**: `_/\` is rejected.
5. **Tree-based mutations**: Mutations operate strictly on tree structures, never raw string manipulation.
6. **Explicit failure modes**: Invalid states and version mismatches fail loudly with distinct error codes.

---

## Wire & Value Formats

EdgeTG has two wire modes for the topology and two value-payload schemes. They are independent and intentionally separate.

### 1. ASCII topology wire mode
Topology glyphs are sent as raw ASCII bytes (`_`, `/`, `\`).
- Packet envelope: `[wire_format = 0x00][topology ASCII bytes]`
- Used for authoring and debugging; this is the human-readable form. Examples in `test_dual_wire_format.c`.

### 2. 2-bit packed topology wire mode
`ts_packed.c` stores **4 topology symbols per byte**, 2 bits each (`_`=0, `/`=1, `\`=2). Code `3` is invalid and rejected on unpack. Packed payload length is `(symbol_count + 3) / 4` bytes.
- Packet envelope: `[wire_format = 0x01][symbol_count u8][packed topology bytes]`
- This is a **transport optimization only**: unpacking always yields exactly the same topology string as ASCII mode, which then feeds the same `ts_parse`/`ts_parse_n`.
- Savings approach ~4x for larger topology strings, but are smaller for tiny packets because of the fixed envelope overhead. Typical demo packets carry a handful of symbols and save only ~1.5x.

The dispatcher rejects any unknown `wire_format` byte rather than guessing.

### 3. Older length-prefixed value blob (`ts_values_encode`)
`ts_values_encode`/`ts_values_decode` serialize the preorder-indexed value array as:
`[u32 little-endian count]` then `count × [u32 little-endian length][value bytes]`.
This is the binary side-artifact paired with the ASCII topology path. `gateway.lua`, `mcu_executor.c`, and `gateway.py` all agree on this format.

### 4. Newer compact demo value payload
`gateway_encode.lua`, `mcu_firmware.c`, and `gateway_decode.py` use a small **manifest-typed** payload: one `u8` bool followed by one `f32` float (5 bytes). This is **demo-specific** and separate from `ts_values_encode`; it is tied directly to the v2 manifest (`valve` = bool, `temp` = float). The firmware reads these positionally, applies `temp += 0.4`, and re-serializes the same 5-byte shape in its packed reply.

> Verification note: ASan/UBSan/LSan require `libasan`/`libubsan` from the compiler runtime. Some Windows MinGW toolchains ship compilers that support the sanitizer flags but omit those runtime libraries, in which case local sanitized runs cannot link. The suites still pass under `-Werror`; sanitized execution is intended on a toolchain that provides the sanitizer runtime.

---

## License

[MIT License](LICENSE)
