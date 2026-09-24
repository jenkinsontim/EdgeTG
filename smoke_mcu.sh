#!/bin/sh
# smoke_mcu.sh — wire-level accept/reject checks for mcu_executor.
# mcu_executor exit codes: 0 ok, 2 wire packet rejected, 3 malformed
# topology, 4 value blob rejected. Runs in a temp dir (the executor writes
# reply_*.bin into its working directory).
set -u
BIN="$(cd "$(dirname "$0")" && pwd)/mcu_executor"
[ -x "$BIN" ] || { echo "smoke_mcu: $BIN not built"; exit 1; }
T="$(mktemp -d)" || exit 1
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 1

pass=0; fail=0
# Value blob with 3 empty values (matches the 3-node tree _/__\).
printf '\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' > v3.bin

check() { # name expected_rc packet_file values_file
    "$BIN" "$3" "$4" > out.txt 2>&1
    rc=$?
    if [ "$rc" -eq "$2" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL: $1 (exit $rc, expected $2)"
        sed 's/^/    | /' out.txt | head -5
    fi
}

printf '\000_/__\\'        > p.bin; check "ascii packet accepted"            0 p.bin v3.bin
printf '\001\005\004\002'  > p.bin; check "packed packet accepted"           0 p.bin v3.bin
printf '\007_'             > p.bin; check "unknown format byte rejected"     2 p.bin v3.bin
printf '\001\005\004\003'  > p.bin; check "packed code 3 rejected"           2 p.bin v3.bin
printf '\001\005\004'      > p.bin; check "packed length mismatch rejected"  2 p.bin v3.bin
printf '\000_/_'           > p.bin; check "malformed topology rejected"      3 p.bin v3.bin
printf '\000_\000/__\\'    > p.bin; check "embedded NUL in ascii rejected"   2 p.bin v3.bin
printf '\000_/__\\\000'    > p.bin; check "trailing NUL in ascii rejected"   2 p.bin v3.bin
printf '\000_/__\\'        > p.bin
printf '\377\377\377\377\000\000\000\000' > vh.bin
check "hostile value count rejected" 4 p.bin vh.bin

echo "smoke_mcu: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
