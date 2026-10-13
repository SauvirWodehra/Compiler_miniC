// ir.h - three-address intermediate representation.
//
// A Function is a control-flow graph of basic blocks. Every block ends in
// exactly one terminator (jmp / br / ret). Values live in an unbounded set of
// virtual registers (%0, %1, ...); the register allocator later maps them onto
// the 5 callee-saved x86-64 registers or onto stack slots.
//
// The IR is deliberately *not* SSA: a source variable is one virtual register
// that may be assigned many times. That keeps IR generation simple and makes
// the optimizer work with classic dataflow analyses (liveness, local value
// numbering) instead of phi nodes.
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

enum class Op {
  Copy,                       // dst = a
  Add, Sub, Mul, Div, Mod,    // dst = a <op> b   (32-bit signed, wrapping)
  Shl,                        // dst = a << b     (only produced by strength reduction)
  Neg, Not,                   // dst = -a, dst = !a
  Lt, Le, Gt, Ge, Eq, Ne,     // dst = (a <op> b) ? 1 : 0
  Param,                      // dst = incoming argument #a
  Call,                       // dst = callee(args...)   (dst = -1 for void)
  Jmp,                        // goto t1
  Br,                         // if (a != 0) goto t1 else goto t2
  Ret,                        // return a (a may be None for void)
};

struct Value {
  enum Kind { None, Reg, Imm } kind = None;
  int64_t v = 0;

  static Value reg(int r) { return Value{Reg, r}; }
  static Value imm(int64_t x) { return Value{Imm, x}; }
  bool isReg() const { return kind == Reg; }
  bool isImm() const { return kind == Imm; }
  bool operator==(const Value& o) const { return kind == o.kind && v == o.v; }
  bool operator!=(const Value& o) const { return !(*this == o); }
};

struct Instr {
  explicit Instr(Op o = Op::Copy) : op(o) {}
  Op op;
  int dst = -1;
  Value a, b;
  std::vector<Value> args;  // Call
  std::string callee;       // Call
  int t1 = -1, t2 = -1;     // Jmp / Br targets (block ids)
};

struct Block {
  int id = 0;
  std::string name;
  std::vector<Instr> ins;
  std::vector<int> succ, pred;
  bool dead = false;
  int seq = 0;  // layout order (order in which IR generation started the block)
};

struct Function {
  std::string name;
  bool returnsValue = true;
  int nparams = 0;
  std::vector<Block> blocks;          // blocks[0] is the entry block
  int nregs = 0;
  std::vector<std::string> regNames;  // source-variable name per vreg ("" for temporaries)
};

struct Module {
  std::vector<Function> funcs;
};

// --- instruction classification -------------------------------------------
bool isTerminator(Op op);
bool isBinary(Op op);
bool isUnary(Op op);
bool isCompare(Op op);
bool isCommutative(Op op);
bool hasSideEffects(Op op);  // calls, params, control flow: never deleted/CSE'd
const char* opName(Op op);

// The virtual register written by `in`, or -1.
int defOf(const Instr& in);
// Appends every virtual register read by `in` to `out`.
void usesOf(const Instr& in, std::vector<int>& out);
// Visits every operand slot of `in` (for rewriting).
template <typename F>
void forEachOperand(Instr& in, F&& f) {
  f(in.a);
  f(in.b);
  for (auto& v : in.args) f(v);
}

// --- CFG utilities ----------------------------------------------------------
void computeCFG(Function& fn);   // rebuilds succ/pred from terminators
void compact(Function& fn);      // drops dead blocks, renumbers, orders by seq
int countInstrs(const Function& fn);

// --- printing ---------------------------------------------------------------
std::string valueStr(const Function& fn, const Value& v);
void printFunction(const Function& fn, std::ostream& os);
void printModule(const Module& m, std::ostream& os);
