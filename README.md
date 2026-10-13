# mcc — An Optimizing Mini-C Compiler

> A from-scratch compiler for a subset of C, written in C++17 (~2,350 LOC, zero
> external libraries). It compiles source code to **x86-64 assembly** through a
> custom **three-address IR**, with a **10-pass optimization pipeline** and a
> **linear-scan register allocator** (Poletto & Sarkar). Its output is
> differentially tested against GCC by a hand-written test suite and a
> random-program fuzzer.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Key Features](#key-features)
- [Getting Started](#getting-started)
- [Usage](#usage)
- [Supported Language Subset](#supported-language-subset)
- [Compiler Phases In Depth](#compiler-phases-in-depth)
  - [1. Lexer](#1-lexer)
  - [2. Parser](#2-parser)
  - [3. AST & Semantic Analysis](#3-ast--semantic-analysis)
  - [4. IR Generation](#4-ir-generation)
  - [5. Optimization Pipeline](#5-optimization-pipeline)
  - [6. Liveness Analysis](#6-liveness-analysis)
  - [7. Register Allocation](#7-register-allocation)
  - [8. Code Generation (x86-64)](#8-code-generation-x86-64)
- [Example: End-to-End Walkthrough](#example-end-to-end-walkthrough)
- [Benchmarks](#benchmarks)
- [Testing Strategy](#testing-strategy)
- [Project Layout](#project-layout)
- [Design Decisions & Trade-offs](#design-decisions--trade-offs)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                              mcc Compiler Pipeline                               │
│                                                                                  │
│  source.c ──► Lexer ──► Parser ──► AST ──► IR Gen ──► Optimizer ──► RegAlloc ──► Codegen ──► .s file
│               │          │          │        │          │             │             │
│               │          │          │        │          │             │             │
│               ▼          ▼          ▼        ▼          ▼             ▼             ▼
│            tokens     recursive   tree    CFG of     10 passes    linear-scan   AT&T syntax
│            maximal    descent     with    basic      iterated     Poletto &     System V ABI
│            munch      1 fn/prec   scope   blocks     to fixed     Sarkar        gcc link
│                       level      info    + vregs     point        5 callee-
│                                                                   saved regs
└───────────────────────────────────────────────────────────────────────────────────┘
```

The compiler is organized into **two logical steps**:

| Step | What | Optimization | Files |
|------|------|-------------|-------|
| **Step 1** — Front end + baseline backend (`-O0`) | Lexer → Parser → AST → semantic checks → IR → x86-64. Every virtual register lives in a stack slot. | None | `lexer.*`, `parser.*`, `ast.h`, `irgen.*`, `ir.*`, `codegen.*` |
| **Step 2** — Optimizer + register allocator (`-O1`, default) | Machine-independent IR passes, then linear-scan register allocation and x86-specific peepholes. | 10 passes to fixed point | `opt.*`, `liveness.*`, `regalloc.*` + peepholes in `codegen.cpp` |

---

## Key Features

### Front End
- **Hand-written lexer** — maximal-munch tokenizer; two-char operators (`<=`, `++`, `&&`) are tried before their single-char prefixes. Integer literals are range-checked against `INT_MAX` during scanning. Supports `//` and `/* */` comments, and silently skips `#include` preprocessor lines so that test files remain valid C.
- **Recursive-descent parser** — one function per precedence level for binary operators via a table-driven `parseBinaryLevel(level)`. Left associativity via iteration, right associativity of `=` via recursion. The classic dangling-else is resolved naturally.
- **Precise error reporting** — every error is tagged with `file:line:col` position. All compiler stages share a single `CompileError` exception type.

### Semantic Analysis
- Lexical scoping and variable shadowing
- Detection of: undeclared variables/functions, redeclarations in the same scope, wrong argument counts, `break`/`continue` outside a loop, returning a value from `void` functions and vice versa

### IR & Optimizer (`-O1`)
- **Three-address IR** — a CFG of basic blocks with unlimited virtual registers. Deliberately **not SSA** to keep IR generation simple (no phi nodes, no dominance frontiers needed).
- **Short-circuit evaluation** — `&&`, `||` and `!` lower directly to branches, never producing a 0/1 intermediate value.
- **10 optimization passes** iterated to a fixed point (max 20 iterations):
  1. Global constant propagation (single-def entry-block constants)
  2. Local constant propagation
  3. Copy propagation & coalescing
  4. Constant folding (exact 32-bit wrap-around; never folds `x/0` or `INT_MIN/-1`)
  5. Algebraic simplification (`x+0`, `x*1`, `x-x`, `x*-1 → -x`, `x==x → 1`, …)
  6. Strength reduction (`x * 2^k → x << k`)
  7. Common-subexpression elimination (local value numbering)
  8. Liveness-based dead-code elimination
  9. CFG cleanup: branch folding, unreachable-block removal, jump threading, block merging
  10. Copy coalescing (`t = op …; x = copy t` with single-use `t` → `x = op …`)

### Register Allocation
- **Linear-scan** algorithm (Poletto & Sarkar, 1999) — the same algorithm used by HotSpot's client JIT and V8.
- **5 callee-saved registers** (`rbx`, `r12`–`r15`): values survive function calls without caller-save code.
- Live intervals built from iterative dataflow liveness analysis. Spills evict the interval with the **furthest end point**.
- `-O0` runs the same allocator with **0 registers**, so every vreg gets a stack slot.

### Code Generation
- **x86-64 AT&T syntax**, System V ABI, assembled and linked by `gcc`.
- **Compare + branch fusion** — `cmp` + `jcc` instead of `setcc` + `movzb` + `cmp $0` + `jne`.
- **Signed div/mod by power-of-two** — arithmetic shift with a rounding bias instead of `idiv`.
- **Immediate folding** — constants are embedded directly into instructions.
- **Fall-through optimization** — no jump emitted when the target is the next block in layout order; branches are inverted so the fall-through edge is free.
- **32-bit arithmetic** throughout (`addl`, `imull`, `idivl`), ensuring C `int` overflow wraps identically to `gcc -fwrapv`.

### Testing & Correctness
- **22 hand-written differential tests** + **9 must-fail error programs** = 53 checks (each valid test is run at both `-O0` and `-O1`).
- **Random-program fuzzer** generates UB-free programs and has passed **2,000+ seeds** with zero mismatches vs. GCC.
- The fuzzer was **validated by fault injection**: a planted bug in constant folding (swapping `sub` operands) was caught in 13 of 60 programs.

---

## Getting Started

### Prerequisites

| Requirement | Notes |
|-------------|-------|
| **Linux x86-64** or **WSL** | The generated assembly uses Linux System V ABI |
| **C++17 compiler** | `g++` ≥ 7 or `clang++` ≥ 5 |
| **gcc** | Used to assemble and link the generated `.s` files |
| **python3** | Only needed for the fuzzer (`scripts/fuzz.py`) |

### Build

```sh
git clone https://github.com/<user>/mini-c-compiler.git
cd mini-c-compiler
make            # builds ./mcc
```

### Quick Test

```sh
make test       # run the differential test suite (53 checks)
make fuzz       # 300 random programs vs gcc
make bench      # benchmark: -O0 vs -O1 vs gcc -O0
```

### Hello World

```sh
# Write a program
cat > hello.c << 'EOF'
int print(int);

int main() {
  print(42);
  return 0;
}
EOF

# Compile and run
./mcc -O1 hello.c -o hello.s
gcc hello.s runtime/runtime.c -o hello
./hello
# Output: 42
```

---

## Usage

```
mcc [-O0|-O1] [--emit-tokens|--emit-ast|--emit-ir] [--stats] [-o out.s] input.c
```

| Flag | Description |
|------|-------------|
| `-O0` | Baseline stack-based code generation — every virtual register on the stack |
| `-O1` | **(default)** Optimizer + register allocation |
| `--emit-tokens` | Dump the token stream and exit |
| `--emit-ast` | Pretty-print the syntax tree and exit |
| `--emit-ir` | Dump the IR (after optimization at `-O1`) and exit |
| `--stats` | Print per-pass rewrite counts, register allocation stats, and instruction counts to stderr |
| `-o file` | Output assembly file (default: `a.s`; use `-` for stdout) |
| `-h`, `--help` | Print usage |

### Debugging Workflow

```sh
# See what the lexer produces
./mcc --emit-tokens examples/demo.c

# Inspect the AST
./mcc --emit-ast examples/demo.c

# See the optimized IR (great for understanding the optimizer)
./mcc -O1 --emit-ir examples/demo.c

# Compare unoptimized vs optimized IR
./mcc -O0 --emit-ir examples/demo.c > ir_O0.txt
./mcc -O1 --emit-ir examples/demo.c > ir_O1.txt
diff ir_O0.txt ir_O1.txt

# Get detailed statistics
./mcc -O1 --stats examples/demo.c -o /dev/null
```

---

## Supported Language Subset

| Category | Details |
|----------|---------|
| **Types** | `int` (32-bit signed, wrapping arithmetic) and `void` |
| **Functions** | Up to 6 parameters (System V ABI limit for integer regs), prototypes, recursion, mutual recursion |
| **Statements** | local variable declarations with block scoping & shadowing, `if`/`else`, `while`, `for`, `break`, `continue`, `return` |
| **Operators** | `+ - * / %`, `< <= > >= == !=`, `&& \|\| !` (short-circuit), unary `-`, `= += -= *= /= %=`, prefix/postfix `++ --` |
| **External calls** | Any declared function (e.g., `int print(int);` from `runtime/runtime.c`) |
| **Comments** | `//` line comments, `/* */` block comments |
| **Preprocessor** | `#include` lines are silently skipped (for test-file compatibility with GCC) |

**Not yet supported:** pointers, arrays, structs, global variables, strings, floating-point types, `switch`/`case`, `do`/`while`.

---

## Compiler Phases In Depth

### 1. Lexer

**Files:** [`lexer.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/lexer.h), [`lexer.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/lexer.cpp) (136 lines)

A single-pass, hand-written scanner. No regular expressions or generator tools.

- **Token types:** `Num`, `Ident`, `Keyword`, `Punct`, `Eof`
- **Keywords:** `int`, `void`, `return`, `if`, `else`, `while`, `for`, `break`, `continue`
- **Maximal munch:** two-char punctuators (`<=`, `++`, `&&`, …) are listed before their single-char prefixes in a table. The first match wins.
- **Integer range check:** integer literals are validated against `INT_MAX` during scanning — an overflow is a compile error, not silent truncation.
- **Precise positions:** every token records its `line` and `col` for error reporting.

### 2. Parser

**Files:** [`parser.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/parser.h), [`parser.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/parser.cpp) (427 lines)

A predictive recursive-descent parser. The grammar is:

```
program     = funcDecl*
funcDecl    = ("int" | "void") IDENT "(" params? ")" (block | ";")
block       = "{" stmt* "}"
stmt        = block | decl | if | while | for | return | break | continue | exprStmt
expr        = assign
assign      = logor ( ("=" | "+=" | "-=" | "*=" | "/=" | "%=") assign )?
logor       = logand ( "||" logand )*
...                                 ← one function per precedence level
unary       = ("-" | "!" | "++" | "--") unary | postfix
postfix     = primary ( "(" args ")" | "++" | "--" )*
primary     = NUM | IDENT | "(" expr ")"
```

- Binary operator precedence is table-driven: `parseBinaryLevel(level)` walks a static table of `{operators, next-level}` pairs.
- Left associativity comes from looping within each level; right associativity of assignment comes from recursion.
- The dangling-else ambiguity is resolved by binding `else` to the nearest unmatched `if`, which falls out naturally from the recursive structure.
- **AST dump** (`--emit-ast`) is implemented in `parser.cpp` via `dumpAST()`.

### 3. AST & Semantic Analysis

**Files:** [`ast.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/ast.h) (50 lines), [`irgen.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/irgen.h) / [`irgen.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/irgen.cpp) (407 lines)

The AST uses `std::unique_ptr` for ownership. Node kinds:
- **Expressions:** `Num`, `Var`, `Binary`, `Unary`, `Assign`, `Call`, `IncDec`
- **Statements:** `Block`, `Decl`, `ExprStmt`, `If`, `While`, `For`, `Return`, `Break`, `Continue`, `Empty`
- **Top-level:** `FuncDecl` (with optional `body` — `nullptr` for prototypes), `Program`

Semantic checks performed during IR generation (`irgen.cpp`):
- Undeclared variable / function use
- Redeclaration in the same scope
- Wrong number of arguments in function calls
- `break` or `continue` outside a loop
- Returning a value from `void`, or missing return value from `int`

### 4. IR Generation

**Files:** [`ir.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/ir.h) (106 lines), [`ir.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/ir.cpp), [`irgen.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/irgen.cpp)

The IR is a **CFG of basic blocks** containing three-address instructions. Every block ends in exactly one terminator (`Jmp`, `Br`, or `Ret`).

**IR instruction set:**

| Category | Opcodes |
|----------|---------|
| Data movement | `Copy` |
| Arithmetic | `Add`, `Sub`, `Mul`, `Div`, `Mod`, `Shl`, `Neg`, `Not` |
| Comparison | `Lt`, `Le`, `Gt`, `Ge`, `Eq`, `Ne` |
| Control flow | `Jmp`, `Br` (conditional), `Ret` |
| Calls | `Call`, `Param` (incoming argument) |

**Key design:** the IR is **not SSA**. A source variable maps to a single virtual register that may be assigned multiple times. This avoids phi nodes and dominance frontiers entirely. The trade-off is that dataflow facts are only trusted:
- **Locally** — within a single basic block, tracking redefinitions.
- **Globally** — only for single-assignment constants in the entry block (which dominates all other blocks).

**Short-circuit:** `&&`, `||`, `!` are lowered directly to conditional branches via `genBranch(expr, trueBlock, falseBlock)`. The expression `if (a && b)` becomes two `Br` instructions — no intermediate boolean value is ever created.

### 5. Optimization Pipeline

**Files:** [`opt.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/opt.h), [`opt.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/opt.cpp) (485 lines)

All passes run in a loop (up to 20 iterations) until a fixed point is reached. Each pass creates work for the others:

```
Constant propagation  →  exposes a constant branch
Branch folding        →  makes a block unreachable
Block removal         →  lets two adjacent blocks merge
Block merging         →  gives local value numbering a longer straight line
```

**Pass details:**

| # | Pass | Implementation |
|---|------|---------------|
| 1 | **Global constant propagation** | Finds vregs assigned exactly once in the entry block with a constant value. Replaces all uses across the entire function. |
| 2 | **Local constant / copy propagation** | Per-block forward walk. Maintains a map `vreg → known value`. Kills facts when a register is redefined. |
| 3 | **Constant folding** | Evaluates operators on two immediates with **exact 32-bit wrap-around** (`wrap32()`). Never folds `x/0` or `INT_MIN/-1` — these must trap at run time. |
| 4 | **Algebraic simplification** | Identity rewrites: `x+0→x`, `x*1→x`, `x*0→0`, `x*-1→-x`, `x-x→0`, `x==x→1`, `x%1→0`, etc. |
| 5 | **Canonicalization** | Immediates are moved to the right operand (`5 < x → x > 5`), so folding, CSE, and x86 instruction selection only handle one shape. |
| 6 | **Strength reduction** | `x * 2^k → x << k` using exact power-of-two detection. |
| 7 | **CSE** | Local value numbering: a repeated pure expression in the same block reuses the earlier result. Commutative operands match in either order. |
| 8 | **Dead-code elimination** | Backward walk with liveness. Drops pure instructions whose result is never read. Calls are always kept (side effects). |
| 9 | **CFG simplification** | Branch folding (`br 1, A, B → jmp A`), unreachable-block removal (DFS from entry), jump threading (trampoline bypass with cycle detection), block merging (absorb single-predecessor successor). |
| 10 | **Copy coalescing** | `t = op …; x = copy t` where `t` has exactly one def and one use → `x = op …`. Eliminates a move and frees a register. |

### 6. Liveness Analysis

**Files:** [`liveness.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/liveness.h), [`liveness.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/liveness.cpp) (57 lines)

Classic backward dataflow analysis:

```
out[b] = ∪ in[s]        for every successor s of block b
in[b]  = use[b] ∪ (out[b] − def[b])
```

Iterated to a fixed point over dense bitsets (`vector<vector<bool>>`), visiting blocks in reverse layout order (which converges quickly for backward problems). Used by both **dead-code elimination** and the **register allocator**.

### 7. Register Allocation

**Files:** [`regalloc.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/regalloc.h), [`regalloc.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/regalloc.cpp) (122 lines)

Linear scan (Poletto & Sarkar, 1999):

1. **Number instructions** in layout order. Uses sit at even positions, defs at position + 1. This allows an operand that dies at an instruction to share a register with that instruction's result.
2. **Build live intervals** `[start, end]` per vreg from def/use positions. Intervals are widened to the block start when the vreg is live-in, and to the block end when live-out. **This is what makes loops correct:** a value live across a back edge covers the entire loop body.
3. **Sweep** intervals sorted by start point. Expire intervals that have ended; assign a free register, or **spill the interval that ends furthest away** to free a register for the longest time.

**Register pool:** only 5 callee-saved registers (`rbx`, `r12`–`r15`). Values survive calls without caller-save code, and argument setup can never clobber a still-needed value. The trade-off is fewer registers and push/pop overhead in the prologue/epilogue.

At `-O0`, the allocator is called with `numRegs = 0`, so every vreg gets a stack slot. This means Step 1 and Step 2 share **the exact same backend**.

### 8. Code Generation (x86-64)

**Files:** [`codegen.h`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/codegen.h), [`codegen.cpp`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/src/codegen.cpp) (349 lines)

Targets **Linux x86-64 System V ABI** with **AT&T syntax**.

**Register conventions:**

| Registers | Role |
|-----------|------|
| `%ebx`, `%r12d`–`%r15d` | Allocatable (callee-saved) |
| `%eax`, `%ecx`, `%edx`, `%r11d` | Scratch — never hold a virtual register |
| `%edi`, `%esi`, `%edx`, `%ecx`, `%r8d`, `%r9d` | Argument passing |

**Stack frame layout:**
```
 [rbp+8]     return address
 [rbp]       saved rbp
 [rbp-8...]  saved callee-saved registers (pushq)
 below       spill slots (8 bytes each)
             ← rsp (16-byte aligned at every call)
```

**Peephole optimizations:**

| Peephole | Before | After |
|----------|--------|-------|
| Compare + branch fusion | `setcc` + `movzbl` + `cmp $0` + `jne` | `cmpl` + `jcc` |
| Div by power of 2 | `cltd` + `idivl` | `sarl $31` + `shrl` + `addl` + `sarl $k` |
| Mod by power of 2 | `cltd` + `idivl` (take `%edx`) | bias + `andl $-2^k` + `subl` |
| Fall-through | `jmp .Lnext` | *(nothing)* |
| Mem-to-mem move | *(illegal on x86)* | bounce through `%r11d` |

---

## Example: End-to-End Walkthrough

### Input (`examples/demo.c`)

```c
int print(int);

int sum_scaled(int n) {
  int scale = 4 * 2;          // folded to 8
  int total = 0;
  for (int i = 0; i < n; i++) {
    int a = i * scale;        // i * 8  ->  i << 3
    int b = i * scale;        // same expression: reused (CSE)
    total = total + a + b;
  }
  return total;
  print(-1);                  // unreachable: removed
}
```

### What the optimizer does

Running `./mcc -O1 --emit-ir examples/demo.c` reduces 26 IR instructions to 15:

| Optimization | Effect |
|-------------|--------|
| Constant folding | `4 * 2` → `8` |
| Strength reduction | `i * 8` → `i << 3` |
| CSE | `b = i * scale` reuses the result already computed for `a` |
| Dead-code elimination | `print(-1)` after `return` is removed |
| Copy coalescing | Temporary copies are eliminated |
| Block merging | Simplified CFG |

### Optimized IR (loop body)

```
bb2.for.body:
    %6 = shl %i.4, 3                ; scale folded to 8, i*8 -> i<<3, b reuses a (CSE)
    %10 = add %total.3, %6
    %total.3 = add %10, %6
    %i.4 = add %i.4, 1
    jmp bb1.for.cond
```

### Generated x86-64 (loop)

The loop runs entirely in registers — zero memory accesses in the hot path:

```asm
.Lsum_scaled_1:
    cmpl %ebx, %r13d          # i < n, fused compare+branch
    jge .Lsum_scaled_3
    movl %r13d, %r14d
    shll $3, %r14d            # i * 8 via shift
    addl %r14d, %r12d         # total += a
    addl %r14d, %r12d         # total += b (reused value)
    addl $1, %r13d            # i++
    jmp .Lsum_scaled_1
```

---

## Benchmarks

Run with `make bench`. Times shown are the best of 3 runs on a Linux x86-64 machine.

| Program | asm instrs `-O0` | asm instrs `-O1` | time `-O0` | time `-O1` | `gcc -O0` | Speedup |
|---------|------------------:|------------------:|-----------:|-----------:|----------:|--------:|
| collatz | 88 | 61 | 73 ms | 43 ms | 42 ms | **1.70×** |
| fib(35) | 45 | 40 | 114 ms | 81 ms | 85 ms | **1.41×** |
| matrix_sum | 90 | 57 | 89 ms | 73 ms | 71 ms | **1.22×** |
| primes | 78 | 59 | 319 ms | 268 ms | 294 ms | **1.19×** |

**Key takeaways:**
- `-O1` emits **~28% fewer instructions** on average than `-O0`.
- `-O1` runs **1.2–1.7× faster** than `-O0`.
- mcc `-O1` is **on par with or faster than `gcc -O0`** on all four benchmarks.
- The benchmark programs cover: tight loops with branching (collatz), recursive calls (fib), nested loops with multiple accumulators (matrix_sum), and trial-division loops with modulus (primes).

---

## Testing Strategy

Every test program is valid C, so **GCC serves as the ground-truth oracle**.

### 1. Differential Test Suite (`make test`)

**Script:** [`scripts/run_tests.sh`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/scripts/run_tests.sh)

- Compiles each of the **22 programs** in `tests/programs/` with `gcc -O0 -fwrapv` and with mcc at both `-O0` and `-O1`.
- Asserts **identical stdout and exit status** (44 checks).
- The **9 programs** in `tests/errors/` must each be **rejected** with a diagnostic containing `error:` (9 checks).
- **Total: 53 checks.**

**Test coverage includes:** arithmetic, comparisons, short-circuit logic, `if`/`else`, `while`/`for`, nested loops, recursion, mutual recursion, 6-argument calls, register pressure, compound assignment, scoping/shadowing, constant folding edge cases (overflow), prime sieve, Collatz sequence, dead code after return, void functions, CSE, exit codes, overflow wrapping, div/mod by powers of two.

### 2. Random-Program Fuzzer (`make fuzz`)

**Script:** [`scripts/fuzz.py`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/scripts/fuzz.py) (200 lines)

Generates random Mini-C programs that are **UB-free by construction**:

| UB Category | How it's avoided |
|-------------|-----------------|
| Signed overflow | Both GCC and mcc use `-fwrapv` (wrap-around semantics) |
| Division by zero | Every divisor is `((expr) % 7 + 8)`, giving a value in `[2, 14]` |
| Evaluation order | Expressions are side-effect free; assignments are statements only |
| Infinite loops | Loop counters use dedicated variables never written by the body |

Programs include: helper functions (1–4 params), nested loops, branches, deep expressions, and compound assignments. Failing programs are saved to `out/fuzz/fail_<seed>.c` for minimization.

**Validation:** the fuzzer itself was tested by deliberately **planting a bug** in `opt.cpp` (swapping the operands of `sub` in constant folding). The fuzzer caught the bug in **13 of 60 programs**.

---

## Project Layout

```
mini-c-compiler/
├── src/                          # All compiler source (~2,350 LOC)
│   ├── main.cpp          (146)   # Driver: CLI parsing, pipeline orchestration
│   ├── lexer.h / .cpp    (152)   # Tokenizer (maximal munch)
│   ├── parser.h / .cpp   (435)   # Recursive-descent parser → AST
│   ├── ast.h              (50)   # AST node definitions
│   ├── irgen.h / .cpp    (407)   # AST → IR + semantic analysis
│   ├── ir.h / .cpp       (265)   # IR types, CFG utilities, printer
│   ├── opt.h / .cpp      (501)   # 10 optimization passes
│   ├── liveness.h / .cpp  (57)   # Backward dataflow liveness analysis
│   ├── regalloc.h / .cpp (122)   # Linear-scan register allocation
│   └── codegen.h / .cpp  (362)   # x86-64 instruction selection & frame layout
├── tests/
│   ├── programs/                 # 22 differential-test programs (valid C)
│   └── errors/                   # 9 programs that must be rejected
├── examples/
│   └── demo.c                    # Showcase program for the README example
├── bench/                        # 4 benchmark programs (collatz, fib, matrix_sum, primes)
├── runtime/
│   └── runtime.c                 # Tiny runtime: print(n) → printf("%d\n", n)
├── scripts/
│   ├── run_tests.sh              # Differential test runner
│   ├── fuzz.py                   # Random-program fuzzer
│   └── bench.sh                  # Benchmark runner
├── Makefile                      # Build system
├── DESIGN.md                     # Design decisions & trade-offs (detailed)
├── LICENSE                       # MIT License
└── README.md                     # This file
```

---

## Design Decisions & Trade-offs

The major design choices and their rationale are documented in detail in [`DESIGN.md`](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/DESIGN.md). Here's a summary:

| Decision | Chosen | Alternative (not taken) | Why |
|----------|--------|------------------------|-----|
| Front end | Hand-written | Parser generator (flex/bison) | Precise errors, small grammar, what GCC/Clang do |
| IR form | Non-SSA (vregs with multiple defs) | SSA with phi nodes | No dominance frontier computation needed; simpler IR gen |
| Optimizer scope | Local value numbering + entry-block globals | SSA-based GVN / SCCP | Sufficient without phi nodes; SSA planned for roadmap |
| Register allocator | Linear scan (Poletto & Sarkar) | Graph coloring (Chaitin–Briggs) | Fast, simple, correct; same algorithm as JIT compilers |
| Register pool | 5 callee-saved only | All 14 GPRs | No caller-save needed; argument setup is trivial |
| Constant folding | Exact 32-bit wrapping | Host-width folding | Matches the generated x86 behavior; fuzzer validates it |
| Fixed-point iteration | Up to 20 rounds | Worklist algorithm | Simple; convergence is fast in practice |

---

## Roadmap

- **PTX backend** — lower a small data-parallel kernel DSL to NVIDIA PTX (`ld.global`, `mad.lo.s32`, `%tid.x`) and validate with `ptxas`.
- **SSA form** — dominator tree, phi nodes → enables global value numbering and sparse conditional constant propagation.
- **Loop-invariant code motion** and induction-variable strength reduction.
- **Caller-saved registers** — allocate `rax`, `rcx`, etc. for intervals that don't cross a call. Requires parallel-move resolution for argument setup.
- **Graph-coloring allocator** (Chaitin–Briggs) to compare against linear scan.
- **Arrays and pointers** — extend the type system and IR.
- **`do`/`while`, `switch`/`case`** — extend the parser and IR generation.

---

## Contributing

Contributions are welcome! Here's how to get started:

1. **Fork and clone** the repository.
2. **Build and test** to make sure everything passes:
   ```sh
   make && make test && make fuzz
   ```
3. **Make your changes** in a feature branch.
4. **Add tests** — if you add a feature, add a `.c` file to `tests/programs/` (for valid programs) or `tests/errors/` (for programs that should be rejected). The differential test runner will pick it up automatically.
5. **Run the full test suite** including the fuzzer before submitting.
6. **Submit a pull request** with a clear description.

### Code Style
- C++17, no external dependencies
- `clang-format` (LLVM style) is recommended
- Keep functions short and focused
- Comment non-obvious design choices

---

## License

This project is licensed under the **MIT License** — see the [LICENSE](file:///c:/Users/LOQ/Desktop/Study/Compiler/mini-c-compiler-main/LICENSE) file for details.

Copyright © 2026 Naman Jain
