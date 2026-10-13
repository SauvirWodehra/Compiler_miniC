// opt.cpp - optimization passes.
//
// The IR is not SSA (a variable is one vreg assigned many times), so facts
// are only trusted where they provably hold:
//   * inside one basic block, tracking redefinitions as we walk forward
//     (local value numbering), and
//   * for vregs assigned exactly once, in the entry block (which dominates
//     every other block), for global constant propagation.
#include "opt.h"

#include <climits>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "liveness.h"

namespace {

int64_t wrap32(int64_t x) {
  return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(x)));
}

// Folds with exactly the semantics the generated x86 code would have.
// Returns false for cases that would trap at run time (x/0, INT_MIN/-1).
bool foldBinary(Op op, int64_t a64, int64_t b64, int64_t& out) {
  const int32_t a = static_cast<int32_t>(a64), b = static_cast<int32_t>(b64);
  switch (op) {
    case Op::Add: out = wrap32(int64_t{a} + b); return true;
    case Op::Sub: out = wrap32(int64_t{a} - b); return true;
    case Op::Mul: out = wrap32(int64_t{a} * b); return true;
    case Op::Div:
    case Op::Mod:
      if (b == 0 || (a == INT32_MIN && b == -1)) return false;
      out = op == Op::Div ? a / b : a % b;
      return true;
    case Op::Shl:
      if (b < 0 || b > 31) return false;
      out = wrap32(static_cast<int64_t>(static_cast<uint32_t>(a) << b));
      return true;
    case Op::Lt: out = a < b; return true;
    case Op::Le: out = a <= b; return true;
    case Op::Gt: out = a > b; return true;
    case Op::Ge: out = a >= b; return true;
    case Op::Eq: out = a == b; return true;
    case Op::Ne: out = a != b; return true;
    default: return false;
  }
}

bool foldUnary(Op op, int64_t a64, int64_t& out) {
  const int32_t a = static_cast<int32_t>(a64);
  if (op == Op::Neg) {
    out = wrap32(-int64_t{a});
    return true;
  }
  if (op == Op::Not) {
    out = a == 0;
    return true;
  }
  return false;
}

Op swappedCompare(Op op) {
  switch (op) {
    case Op::Lt: return Op::Gt;
    case Op::Gt: return Op::Lt;
    case Op::Le: return Op::Ge;
    case Op::Ge: return Op::Le;
    default: return op;  // Eq, Ne are symmetric
  }
}

void makeCopy(Instr& in, Value src) {
  Instr c{Op::Copy};
  c.dst = in.dst;
  c.a = src;
  in = c;
}

void makeJump(Instr& in, int target) {
  Instr j{Op::Jmp};
  j.t1 = target;
  in = j;
}

int log2Exact(int64_t k) {
  if (k < 2 || k > (int64_t{1} << 30) || (k & (k - 1)) != 0) return -1;
  int n = 0;
  while ((int64_t{1} << n) != k) ++n;
  return n;
}

// Constant folding, canonicalisation, algebraic identities, strength reduction,
// branch folding. Rewrites `in` in place.
void simplify(Instr& in, OptStats& st) {
  if (isBinary(in.op)) {
    int64_t r;
    if (in.a.isImm() && in.b.isImm() && foldBinary(in.op, in.a.v, in.b.v, r)) {
      makeCopy(in, Value::imm(r));
      st.add("constant-fold");
      return;
    }
    // Canonical form: immediate on the right (x86 cmp/add take imm as source).
    if (in.a.isImm() && in.b.isReg()) {
      if (isCommutative(in.op)) {
        std::swap(in.a, in.b);
      } else if (isCompare(in.op)) {
        std::swap(in.a, in.b);
        in.op = swappedCompare(in.op);
      }
    }
    if (in.b.isImm()) {
      const int64_t k = in.b.v;
      if ((in.op == Op::Add || in.op == Op::Sub || in.op == Op::Shl) && k == 0) {
        makeCopy(in, in.a);
        st.add("algebraic-simplify");
        return;
      }
      if ((in.op == Op::Mul || in.op == Op::Div) && k == 1) {
        makeCopy(in, in.a);
        st.add("algebraic-simplify");
        return;
      }
      if ((in.op == Op::Mul && k == 0) || (in.op == Op::Mod && (k == 1 || k == -1))) {
        makeCopy(in, Value::imm(0));
        st.add("algebraic-simplify");
        return;
      }
      if (in.op == Op::Mul && k == -1) {
        in.op = Op::Neg;
        in.b = Value{};
        st.add("algebraic-simplify");
        return;
      }
      int sh;
      if (in.op == Op::Mul && in.a.isReg() && (sh = log2Exact(k)) > 0) {
        in.op = Op::Shl;  // x * 2^k  ->  x << k
        in.b = Value::imm(sh);
        st.add("strength-reduce");
        return;
      }
    }
    // x - x, x == x, x < x, ...
    if (in.a.isReg() && in.a == in.b) {
      switch (in.op) {
        case Op::Sub: case Op::Ne: case Op::Lt: case Op::Gt:
          makeCopy(in, Value::imm(0));
          st.add("algebraic-simplify");
          return;
        case Op::Eq: case Op::Le: case Op::Ge:
          makeCopy(in, Value::imm(1));
          st.add("algebraic-simplify");
          return;
        default:
          break;
      }
    }
    return;
  }

  if (isUnary(in.op) && in.a.isImm()) {
    int64_t r;
    if (foldUnary(in.op, in.a.v, r)) {
      makeCopy(in, Value::imm(r));
      st.add("constant-fold");
    }
    return;
  }

  if (in.op == Op::Br) {
    if (in.a.isImm()) {
      makeJump(in, in.a.v != 0 ? in.t1 : in.t2);
      st.add("branch-fold");
    } else if (in.t1 == in.t2) {
      makeJump(in, in.t1);
      st.add("branch-fold");
    }
  }
}

// ---------------------------------------------------------------------------
// Local value numbering over one basic block.
// ---------------------------------------------------------------------------
struct AvailExpr {
  Op op;
  Value a, b;
  int holder;  // vreg that currently holds the value
};

bool sameExpr(const AvailExpr& e, const Instr& in) {
  if (e.op != in.op) return false;
  if (e.a == in.a && e.b == in.b) return true;
  return isCommutative(in.op) && e.a == in.b && e.b == in.a;
}

bool mentions(const Value& v, int r) { return v.isReg() && v.v == r; }

bool localOptimize(Block& b, OptStats& st) {
  bool changed = false;
  std::unordered_map<int, Value> known;  // vreg -> constant or copy source
  std::vector<AvailExpr> avail;
  std::vector<bool> remove(b.ins.size(), false);

  for (size_t idx = 0; idx < b.ins.size(); ++idx) {
    Instr& in = b.ins[idx];

    // 1. Constant / copy propagation into operands.
    forEachOperand(in, [&](Value& v) {
      if (!v.isReg()) return;
      auto it = known.find(static_cast<int>(v.v));
      if (it != known.end()) {
        v = it->second;
        changed = true;
        st.add(v.isImm() ? "constant-propagate" : "copy-propagate");
      }
    });

    // 2. Folding and peephole algebra.
    Instr before = in;
    simplify(in, st);
    if (in.op != before.op || in.a != before.a || in.b != before.b || in.t1 != before.t1)
      changed = true;

    // 3. Common-subexpression elimination.
    if (isBinary(in.op) || isUnary(in.op)) {
      for (const auto& e : avail) {
        if (sameExpr(e, in)) {
          makeCopy(in, Value::reg(e.holder));
          st.add("cse");
          changed = true;
          break;
        }
      }
    }

    // A self-copy (x = x) does nothing.
    if (in.op == Op::Copy && mentions(in.a, in.dst)) {
      remove[idx] = true;
      changed = true;
      continue;
    }

    // 4. The destination is overwritten: forget every fact that involved it.
    if (in.dst >= 0) {
      const int d = in.dst;
      known.erase(d);
      for (auto it = known.begin(); it != known.end();)
        it = mentions(it->second, d) ? known.erase(it) : std::next(it);
      for (size_t k = 0; k < avail.size();) {
        const auto& e = avail[k];
        if (e.holder == d || mentions(e.a, d) || mentions(e.b, d)) avail.erase(avail.begin() + k);
        else ++k;
      }
    }

    // 5. Record new facts.
    if (in.op == Op::Copy) {
      known[in.dst] = in.a;
    } else if ((isBinary(in.op) || isUnary(in.op)) && !mentions(in.a, in.dst) &&
               !mentions(in.b, in.dst)) {
      avail.push_back({in.op, in.a, in.b, in.dst});
    }
  }

  if (changed) {
    std::vector<Instr> kept;
    for (size_t i = 0; i < b.ins.size(); ++i)
      if (!remove[i]) kept.push_back(std::move(b.ins[i]));
    b.ins = std::move(kept);
  }
  return changed;
}

// ---------------------------------------------------------------------------
// Global constant propagation for vregs with a single, constant definition in
// the entry block. The entry block has no predecessors and dominates every
// block, so any later use must observe that value.
// ---------------------------------------------------------------------------
bool globalConstProp(Function& fn, OptStats& st) {
  if (fn.blocks.empty() || !fn.blocks[0].pred.empty()) return false;
  std::vector<int> defs(fn.nregs, 0);
  for (const auto& b : fn.blocks) {
    if (b.dead) continue;
    for (const auto& in : b.ins)
      if (in.dst >= 0) ++defs[in.dst];
  }

  std::unordered_map<int, std::pair<size_t, int64_t>> consts;  // vreg -> (index, value)
  const auto& entry = fn.blocks[0].ins;
  for (size_t i = 0; i < entry.size(); ++i) {
    const Instr& in = entry[i];
    if (in.op == Op::Copy && in.a.isImm() && defs[in.dst] == 1) consts[in.dst] = {i, in.a.v};
  }
  if (consts.empty()) return false;

  bool changed = false;
  for (auto& b : fn.blocks) {
    if (b.dead) continue;
    for (size_t i = 0; i < b.ins.size(); ++i) {
      forEachOperand(b.ins[i], [&](Value& v) {
        if (!v.isReg()) return;
        auto it = consts.find(static_cast<int>(v.v));
        if (it == consts.end()) return;
        if (b.id == 0 && i <= it->second.first) return;  // use before the def
        v = Value::imm(it->second.second);
        changed = true;
        st.add("global-constant-propagate");
      });
    }
  }
  return changed;
}

// ---------------------------------------------------------------------------
// Dead-code elimination: drop pure instructions whose result is never read.
// ---------------------------------------------------------------------------
bool deadCodeElim(Function& fn, OptStats& st) {
  Liveness lv = computeLiveness(fn);
  bool changed = false;
  std::vector<int> tmp;
  for (auto& b : fn.blocks) {
    if (b.dead) continue;
    std::vector<bool> live = lv.out[b.id];
    std::vector<bool> remove(b.ins.size(), false);
    for (size_t i = b.ins.size(); i-- > 0;) {
      const Instr& in = b.ins[i];
      const bool removable = in.op != Op::Call && !isTerminator(in.op);
      if (in.dst >= 0 && !live[in.dst] && removable) {
        remove[i] = true;
        changed = true;
        st.add("dead-code-elim");
        continue;
      }
      if (in.dst >= 0) live[in.dst] = false;
      tmp.clear();
      usesOf(in, tmp);
      for (int r : tmp) live[r] = true;
    }
    std::vector<Instr> kept;
    for (size_t i = 0; i < b.ins.size(); ++i)
      if (!remove[i]) kept.push_back(std::move(b.ins[i]));
    b.ins = std::move(kept);
  }
  return changed;
}

// ---------------------------------------------------------------------------
// CFG simplification.
// ---------------------------------------------------------------------------
bool removeUnreachable(Function& fn, OptStats& st) {
  computeCFG(fn);
  std::vector<bool> seen(fn.blocks.size(), false);
  std::vector<int> stack = {0};
  seen[0] = true;
  while (!stack.empty()) {
    int b = stack.back();
    stack.pop_back();
    for (int s : fn.blocks[b].succ)
      if (!seen[s]) {
        seen[s] = true;
        stack.push_back(s);
      }
  }
  bool changed = false;
  for (auto& b : fn.blocks) {
    if (!b.dead && !seen[b.id]) {
      b.dead = true;
      changed = true;
      st.add("unreachable-block-elim");
    }
  }
  if (changed) computeCFG(fn);
  return changed;
}

// Blocks that only contain "jmp X" are bypassed: predecessors jump to X directly.
bool threadJumps(Function& fn, OptStats& st) {
  auto isTrampoline = [&](int b) {
    const Block& blk = fn.blocks[b];
    return b != 0 && !blk.dead && blk.ins.size() == 1 && blk.ins[0].op == Op::Jmp &&
           blk.ins[0].t1 != b;
  };
  auto finalTarget = [&](int b) {
    std::unordered_set<int> visited;
    while (isTrampoline(b) && !visited.count(b)) {
      visited.insert(b);
      b = fn.blocks[b].ins[0].t1;
    }
    return b;
  };
  bool changed = false;
  for (auto& b : fn.blocks) {
    if (b.dead || b.ins.empty()) continue;
    Instr& t = b.ins.back();
    for (int* target : {&t.t1, &t.t2}) {
      if (*target < 0) continue;
      int f = finalTarget(*target);
      if (f != *target) {
        *target = f;
        changed = true;
        st.add("jump-thread");
      }
    }
  }
  if (changed) computeCFG(fn);
  return changed;
}

// A block ending in "jmp B", where B has no other predecessor, absorbs B.
bool mergeBlocks(Function& fn, OptStats& st) {
  bool changed = false;
  for (auto& a : fn.blocks) {
    while (!a.dead && !a.ins.empty() && a.ins.back().op == Op::Jmp) {
      int bid = a.ins.back().t1;
      Block& b = fn.blocks[bid];
      if (bid == 0 || bid == a.id || b.pred.size() != 1) break;
      a.ins.pop_back();
      for (auto& in : b.ins) a.ins.push_back(std::move(in));
      b.ins.clear();
      b.dead = true;
      changed = true;
      st.add("block-merge");
      computeCFG(fn);
    }
  }
  return changed;
}

// ---------------------------------------------------------------------------
// Copy coalescing: "t = <op> ...; x = copy t" where t is used only by that
// copy becomes "x = <op> ...". Removes a move and a temporary register.
// Safe because every instruction reads its operands before writing its dst.
// ---------------------------------------------------------------------------
bool coalesceCopies(Function& fn, OptStats& st) {
  std::vector<int> defs(fn.nregs, 0), uses(fn.nregs, 0);
  std::vector<int> tmp;
  for (const auto& b : fn.blocks) {
    if (b.dead) continue;
    for (const auto& in : b.ins) {
      if (in.dst >= 0) ++defs[in.dst];
      tmp.clear();
      usesOf(in, tmp);
      for (int r : tmp) ++uses[r];
    }
  }
  bool changed = false;
  for (auto& b : fn.blocks) {
    if (b.dead) continue;
    for (size_t i = 0; i + 1 < b.ins.size(); ++i) {
      Instr& d = b.ins[i];
      const Instr& c = b.ins[i + 1];
      if (c.op != Op::Copy || !c.a.isReg() || d.dst < 0 || c.a.v != d.dst) continue;
      const int t = d.dst;
      if (defs[t] != 1 || uses[t] != 1 || c.dst == t) continue;
      d.dst = c.dst;
      b.ins.erase(b.ins.begin() + static_cast<long>(i) + 1);
      changed = true;
      st.add("copy-coalesce");
    }
  }
  return changed;
}

}  // namespace

void optimizeFunction(Function& fn, OptStats& st) {
  computeCFG(fn);
  for (int iter = 0; iter < 20; ++iter) {
    bool changed = false;
    changed |= globalConstProp(fn, st);
    for (auto& b : fn.blocks)
      if (!b.dead) changed |= localOptimize(b, st);
    changed |= removeUnreachable(fn, st);
    changed |= deadCodeElim(fn, st);
    changed |= threadJumps(fn, st);
    changed |= removeUnreachable(fn, st);
    changed |= mergeBlocks(fn, st);
    changed |= coalesceCopies(fn, st);
    if (!changed) break;
  }
  compact(fn);
}
