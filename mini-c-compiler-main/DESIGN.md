# Design notes

This file explains why the compiler is built the way it is. Each section names
the trade-off and the alternative that was not taken.

## 1. Front end

**Lexer.** A single pass over the characters. Operators use *maximal munch*:
the punctuator table lists two-character operators first, so `<=` wins over
`<` and `++` over `+`. Integer literals are range-checked against `INT_MAX`
while they are scanned.

**Parser.** Recursive descent, with one function per precedence level for the
binary operators (`parseBinaryLevel(level)` walks a table). Left associativity
comes from looping; right associativity of `=` comes from recursion
(`assign := logor ('=' assign)?`). The classic dangling-else is resolved by
binding `else` to the nearest `if`, which recursive descent does naturally.

**Why not a parser generator?** The grammar is small, hand-written code gives
precise error positions, and it is the approach GCC and Clang use for C.

## 2. IR

A function is a CFG of basic blocks. Each block is a list of three-address
instructions ending in exactly one terminator (`jmp`, `br`, `ret`).
Instruction operands are virtual registers or immediates.

**Not SSA, on purpose.** A source variable is one virtual register that may be
assigned many times. IR generation then needs no phi nodes, so it needs no
dominance frontiers. The cost is that dataflow facts are only trusted where
they provably hold:

- **Inside one block.** Local value numbering walks forward and kills every
  fact mentioning a register when that register is redefined.
- **Globally, for single-assignment constants in the entry block.** The entry
  block has no predecessors and dominates every block, so a vreg assigned once
  there (`x = copy 8`) has that value at every later use.

Converting to SSA is the natural next step (see the README roadmap).

**Short-circuit evaluation.** `genBranch(expr, trueBlock, falseBlock)` lowers
`&&`, `||` and `!` directly into control flow. `if (a && b)` never builds a
0/1 value; it becomes two conditional branches.

## 3. Optimizer

The passes run in a loop until nothing changes, because each pass creates work
for the others. For example:

- Constant propagation exposes a constant branch.
- Branch folding makes a block unreachable.
- Removing it lets two blocks merge.
- The merged block gives local value numbering a longer straight line to work on.

| pass | what it does |
|------|--------------|
| global-constant-propagate | single-def entry-block constants into every use |
| constant/copy-propagate | per-block: replace uses of `x` when `x = 5` or `x = y` is known |
| constant-fold | evaluate operators on immediates, with **exact 32-bit wrap-around**; never folds `x/0` or `INT_MIN/-1` (they trap at run time and must keep doing so) |
| algebraic-simplify | `x+0`, `x*1`, `x*0`, `x*-1 → -x`, `x-x`, `x==x`, … |
| strength-reduce | `x * 2^k → x << k` |
| cse | local value numbering: a repeated pure expression reuses the earlier result |
| dead-code-elim | backward walk with liveness; drops pure instructions whose result is never read (calls are kept for their side effects) |
| branch-fold / unreachable-block-elim | `br 1, A, B → jmp A`, then delete blocks not reachable from entry |
| jump-thread | a block containing only `jmp X` is bypassed |
| block-merge | `A: …; jmp B` where B has one predecessor → concatenate |
| copy-coalesce | `t = op …; x = copy t` with a single-use `t` → `x = op …` |

Canonicalisation helps the later passes. Immediates are moved to the right
operand (`5 < x` becomes `x > 5`), so folding, CSE and x86 instruction
selection only need to handle one shape.

## 4. Liveness

This is a classic backward dataflow problem:

```
out[b] = ∪ in[s]                     for every successor s
in[b]  = use[b] ∪ (out[b] − def[b])
```

It is iterated to a fixed point over dense bitsets, visiting blocks in reverse
layout order (which converges quickly for a backward problem). Both DCE and
the register allocator use it.

## 5. Register allocation — linear scan

Linear scan was chosen over graph coloring because it is fast (one sort plus
one sweep) and simple to get right. That is why JITs use it, for example
HotSpot's client compiler and V8.

1. Number instructions in layout order. Uses sit at even positions and defs
   at position + 1, so an operand that dies at an instruction can share a
   register with that instruction's result.
2. Build one interval `[start, end]` per vreg from its def/use positions.
   Widen it to the block start when the vreg is live-in, and to the block end
   when it is live-out. **This is what makes loops correct:** a value live
   across a back edge covers the whole loop body.
3. Sweep intervals by start point. Expire intervals that have ended, take a
   free register, or, if none is free, **spill the interval that ends
   furthest away**. That frees a register for the longest time.

**Only callee-saved registers** (`rbx`, `r12`–`r15`) are allocated. Values
then survive calls with no caller-save code, and argument setup can never
overwrite a value that is still needed. The trade-off is 5 registers instead
of 14, plus push/pop cost in the prologue. Using caller-saved registers for
intervals that don't cross a call would need parallel-move resolution, which
is on the roadmap.

`-O0` runs the same allocator with 0 registers, so every vreg gets a stack
slot. Step 1 and Step 2 therefore share one backend.

## 6. Code generation (x86-64, System V)

- **32-bit arithmetic** (`addl`, `imull`, `idivl`), so C `int` overflow wraps
  exactly as in the folding code. The fuzzer relies on this to compare
  against `gcc -fwrapv`.
- **Frame layout:** saved `rbp`, then pushed callee-saved registers, then
  spill slots. The frame is padded so `rsp` is 16-byte aligned at every
  `call`, as the ABI requires.
- **Scratch registers** `eax`, `ecx`, `edx`, `r11d` never hold a virtual
  register. That makes `idiv` (which needs `edx:eax`) and memory-to-memory
  moves safe.
- **Peepholes:**
  - **Compare + branch fusion.** A compare whose only use is the next
    branch becomes `cmp` + `jcc` instead of `setcc` + `movzb` + `cmp $0` + `jne`.
  - **Division by a power of two.** An arithmetic shift rounds toward −∞, but
    C truncates toward zero, so negative dividends get a bias of `2^k − 1`
    first: `bias = (x >> 31) >>> (32 − k)`, `x / 2^k = (x + bias) >> k`, and
    `x % 2^k = x − ((x + bias) & −2^k)`.
  - **Fall-through.** No `jmp` to the next block in the layout, and branches
    are inverted so the fall-through edge is free.

## 7. Testing

A compiler is only as good as its testing. Both strategies use GCC as the
oracle, because every test program is valid C:

- **Differential tests.** Hand-written programs, each targeting a feature or
  an edge case (`INT_MIN / 4`, short-circuit side effects, register pressure,
  shadowing).
- **Fuzzing.** Random programs that are UB-free by construction. They
  exercise combinations of passes nobody would write a test for. The fuzzer
  itself was validated by
  planting a deliberate bug in constant folding (swapping the operands of
  `sub`), which it caught in 13 of 60 programs.
