#!/usr/bin/env python3
"""Differential fuzzer: generate random, UB-free Mini-C programs, compile them
with GCC (the oracle) and with mcc at -O0 and -O1, and compare the output.

UB is avoided by construction:
  * signed overflow  -> reference built with -fwrapv (mcc wraps too)
  * division by zero -> every divisor has the form ((e) % 7 + 8), i.e. 2..14
  * evaluation order -> expressions are side-effect free (assignments are
                        statements, helper functions are pure)
  * infinite loops   -> loops count a dedicated variable the body never writes

Failing programs are saved to out/fuzz/fail_<seed>.c for minimisation.
"""
import argparse
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "fuzz")

BIN_OPS = ["+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "||"]


class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.helpers = []  # (name, nparams)
        self.counter = 0

    def fresh(self, prefix):
        self.counter += 1
        return f"{prefix}{self.counter}"

    def const(self):
        r = self.rng.random()
        if r < 0.6:
            return str(self.rng.randint(0, 20))
        if r < 0.8:
            return f"(-{self.rng.randint(1, 50)})"
        return str(self.rng.choice([255, 1024, 65535, 100000, 2147483647, 46341]))

    def expr(self, vars_, depth):
        r = self.rng.random()
        if depth <= 0 or r < 0.25:
            if vars_ and self.rng.random() < 0.7:
                return self.rng.choice(vars_)
            return self.const()
        if r < 0.35:
            return f"{self.rng.choice(['-', '!'])}({self.expr(vars_, depth - 1)})"
        if r < 0.45 and self.helpers:
            name, n = self.rng.choice(self.helpers)
            args = ", ".join(self.expr(vars_, depth - 1) for _ in range(n))
            return f"{name}({args})"
        op = self.rng.choice(BIN_OPS)
        lhs = self.expr(vars_, depth - 1)
        rhs = self.expr(vars_, depth - 1)
        if op in ("/", "%"):
            rhs = f"(({rhs}) % 7 + 8)"
        return f"({lhs} {op} {rhs})"

    def helper(self, idx):
        n = self.rng.randint(1, 4)
        params = [f"p{i}" for i in range(n)]
        name = f"h{idx}"
        lines = [f"int {name}({', '.join('int ' + p for p in params)}) {{"]
        vars_ = list(params)
        for _ in range(self.rng.randint(0, 3)):
            v = self.fresh("l")
            lines.append(f"  int {v} = {self.expr(vars_, 3)};")
            vars_.append(v)
        if self.rng.random() < 0.5:
            lines.append(f"  if ({self.expr(vars_, 2)}) return {self.expr(vars_, 3)};")
        lines.append(f"  return {self.expr(vars_, 3)};")
        lines.append("}")
        self.helpers.append((name, n))
        return "\n".join(lines)

    def block(self, writable, readable, depth, indent, in_loop):
        out = []
        for _ in range(self.rng.randint(1, 4)):
            out.extend(self.stmt(writable, readable, depth, indent, in_loop))
        return out

    def stmt(self, writable, readable, depth, indent, in_loop):
        pad = "  " * indent
        r = self.rng.random()
        if depth <= 0 or r < 0.35:
            v = self.rng.choice(writable)
            op = self.rng.choice(["=", "=", "+=", "-=", "*="])
            return [f"{pad}{v} {op} {self.expr(readable, 3)};"]
        if r < 0.5:
            return [f"{pad}print({self.expr(readable, 3)});"]
        if r < 0.65:
            lines = [f"{pad}if ({self.expr(readable, 3)}) {{"]
            lines += self.block(writable, readable, depth - 1, indent + 1, in_loop)
            if self.rng.random() < 0.5:
                lines.append(f"{pad}}} else {{")
                lines += self.block(writable, readable, depth - 1, indent + 1, in_loop)
            lines.append(f"{pad}}}")
            return lines
        if r < 0.8:
            iv = self.fresh("i")
            lines = [f"{pad}for (int {iv} = 0; {iv} < {self.rng.randint(1, 8)}; {iv}++) {{"]
            lines += self.block(writable, readable + [iv], depth - 1, indent + 1, True)
            lines.append(f"{pad}}}")
            return lines
        if r < 0.9:
            wv = self.fresh("w")
            lines = [f"{pad}int {wv} = 0;", f"{pad}while ({wv} < {self.rng.randint(1, 6)}) {{"]
            lines += self.block(writable, readable + [wv], depth - 1, indent + 1, True)
            lines.append(f"{pad}  {wv}++;")
            lines.append(f"{pad}}}")
            # wv stays in scope after the loop; it's fine to read but not needed.
            return lines
        if in_loop:
            return [f"{pad}if ({self.expr(readable, 2)}) break;"]
        return [f"{pad}print({self.expr(readable, 2)});"]

    def program(self):
        parts = ["int print(int);"]
        for i in range(self.rng.randint(0, 3)):
            parts.append(self.helper(i))
        vars_ = [f"v{i}" for i in range(self.rng.randint(2, 7))]
        body = ["int main() {"]
        for v in vars_:
            body.append(f"  int {v} = {self.const()};")
        body += self.block(vars_, list(vars_), 3, 1, False)
        for v in vars_:
            body.append(f"  print({v});")
        body.append("  return 0;")
        body.append("}")
        parts.append("\n".join(body))
        return "\n\n".join(parts) + "\n"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=20, **kw)


def check(seed, mcc, runtime_obj):
    src = Gen(random.Random(seed)).program()
    path = os.path.join(OUT, f"case_{seed}.c")
    with open(path, "w") as f:
        f.write(src)

    ref_bin = os.path.join(OUT, f"case_{seed}.ref")
    r = run(["gcc", "-w", "-O0", "-fwrapv", path, os.path.join(ROOT, "runtime", "runtime.c"), "-o", ref_bin])
    if r.returncode != 0:
        return None  # generator bug, not a compiler bug
    expected = run([ref_bin])

    for opt in ("-O0", "-O1"):
        asm = os.path.join(OUT, f"case_{seed}{opt}.s")
        exe = os.path.join(OUT, f"case_{seed}{opt}")
        r = run([mcc, opt, path, "-o", asm])
        if r.returncode != 0:
            return f"{opt}: mcc rejected program: {r.stderr.strip()}"
        r = run(["gcc", asm, runtime_obj, "-o", exe])
        if r.returncode != 0:
            return f"{opt}: assembly failed: {r.stderr.strip()[:200]}"
        got = run([exe])
        if got.stdout != expected.stdout or got.returncode != expected.returncode:
            return f"{opt}: output differs from gcc"
    for p in os.listdir(OUT):
        if p.startswith(f"case_{seed}"):
            os.remove(os.path.join(OUT, p))
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    mcc = os.path.join(ROOT, "mcc")
    runtime_obj = os.path.join(OUT, "runtime.o")
    run(["gcc", "-c", os.path.join(ROOT, "runtime", "runtime.c"), "-o", runtime_obj])

    passed = failed = skipped = 0
    for seed in range(args.seed, args.seed + args.count):
        res = check(seed, mcc, runtime_obj)
        if res is None:
            skipped += 1
        elif res == "":
            passed += 1
        else:
            failed += 1
            os.replace(os.path.join(OUT, f"case_{seed}.c"), os.path.join(OUT, f"fail_{seed}.c"))
            print(f"seed {seed}: {res}  (saved out/fuzz/fail_{seed}.c)")
    print(f"fuzz: {passed} passed, {failed} failed, {skipped} skipped")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
