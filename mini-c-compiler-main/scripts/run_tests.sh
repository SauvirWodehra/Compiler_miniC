#!/usr/bin/env bash
# Differential test suite: every program in tests/programs is valid C, so GCC
# is the oracle. Each program is compiled by GCC and by mcc at -O0 and -O1;
# stdout and the exit status must match exactly.
# Programs in tests/errors must be rejected with a diagnostic.
set -u
cd "$(dirname "$0")/.."

MCC=./mcc
OUT=out/tests
mkdir -p "$OUT"
gcc -c runtime/runtime.c -o "$OUT/runtime.o"

pass=0
fail=0

for src in tests/programs/*.c; do
  name=$(basename "$src" .c)
  # -fwrapv: signed overflow wraps, matching mcc's 32-bit semantics.
  gcc -w -O0 -fwrapv "$src" runtime/runtime.c -o "$OUT/$name.ref"
  "$OUT/$name.ref" > "$OUT/$name.expected"
  ref_status=$?

  for opt in O0 O1; do
    if ! $MCC -$opt "$src" -o "$OUT/$name.$opt.s" 2> "$OUT/$name.$opt.err"; then
      echo "FAIL  $name (-$opt): mcc error: $(cat "$OUT/$name.$opt.err")"
      fail=$((fail + 1))
      continue
    fi
    if ! gcc "$OUT/$name.$opt.s" "$OUT/runtime.o" -o "$OUT/$name.$opt" 2> "$OUT/$name.$opt.err"; then
      echo "FAIL  $name (-$opt): assembler/linker error: $(head -3 "$OUT/$name.$opt.err")"
      fail=$((fail + 1))
      continue
    fi
    "$OUT/$name.$opt" > "$OUT/$name.$opt.actual"
    status=$?
    if [ "$status" -ne "$ref_status" ] || ! cmp -s "$OUT/$name.expected" "$OUT/$name.$opt.actual"; then
      echo "FAIL  $name (-$opt): output or exit status differs from gcc (got $status, want $ref_status)"
      diff "$OUT/$name.expected" "$OUT/$name.$opt.actual" | head -5
      fail=$((fail + 1))
    else
      pass=$((pass + 1))
    fi
  done
done

for src in tests/errors/*.c; do
  name=$(basename "$src" .c)
  if $MCC "$src" -o "$OUT/err_$name.s" 2> "$OUT/err_$name.txt"; then
    echo "FAIL  error/$name: expected a compile error, but mcc accepted it"
    fail=$((fail + 1))
  elif ! grep -q "error:" "$OUT/err_$name.txt"; then
    echo "FAIL  error/$name: no diagnostic printed"
    fail=$((fail + 1))
  else
    pass=$((pass + 1))
  fi
done

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
