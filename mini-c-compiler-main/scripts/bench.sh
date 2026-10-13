#!/usr/bin/env bash
# Compares mcc -O0 (everything on the stack) with mcc -O1 (optimizer + linear
# scan) on the programs in bench/: generated instruction count and run time,
# with gcc -O0 as a reference point.
set -eu
cd "$(dirname "$0")/.."
OUT=out/bench
mkdir -p "$OUT"
gcc -c runtime/runtime.c -o "$OUT/runtime.o"

best_time() {  # best of 3 runs, in milliseconds
  local exe=$1 best=999999
  for _ in 1 2 3; do
    local s e ms
    s=$(date +%s%N); "$exe" > /dev/null; e=$(date +%s%N)
    ms=$(( (e - s) / 1000000 ))
    [ "$ms" -lt "$best" ] && best=$ms
  done
  echo "$best"
}

count_insns() { grep -cE '^    [a-z]' "$1" | tr -d ' '; }

printf "%-14s %8s %8s %9s %9s %9s %8s\n" program "asm -O0" "asm -O1" "ms -O0" "ms -O1" "gcc -O0" speedup
for src in bench/*.c; do
  name=$(basename "$src" .c)
  for opt in O0 O1; do
    ./mcc -$opt "$src" -o "$OUT/$name.$opt.s"
    gcc "$OUT/$name.$opt.s" "$OUT/runtime.o" -o "$OUT/$name.$opt"
  done
  gcc -w -O0 -fwrapv "$src" runtime/runtime.c -o "$OUT/$name.gcc"
  a0=$(count_insns "$OUT/$name.O0.s"); a1=$(count_insns "$OUT/$name.O1.s")
  t0=$(best_time "$OUT/$name.O0"); t1=$(best_time "$OUT/$name.O1"); tg=$(best_time "$OUT/$name.gcc")
  speed=$(awk -v a="$t0" -v b="$t1" 'BEGIN { if (b == 0) print "-"; else printf "%.2fx", a / b }')
  printf "%-14s %8s %8s %9s %9s %9s %8s\n" "$name" "$a0" "$a1" "$t0" "$t1" "$tg" "$speed"
done
