// ir.cpp - IR helpers: classification, CFG maintenance, pretty printer.
#include "ir.h"

#include <algorithm>
#include <numeric>

bool isTerminator(Op op) { return op == Op::Jmp || op == Op::Br || op == Op::Ret; }

bool isBinary(Op op) {
  switch (op) {
    case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod: case Op::Shl:
    case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne:
      return true;
    default:
      return false;
  }
}

bool isUnary(Op op) { return op == Op::Neg || op == Op::Not; }

bool isCompare(Op op) {
  return op == Op::Lt || op == Op::Le || op == Op::Gt || op == Op::Ge || op == Op::Eq ||
         op == Op::Ne;
}

bool isCommutative(Op op) {
  return op == Op::Add || op == Op::Mul || op == Op::Eq || op == Op::Ne;
}

bool hasSideEffects(Op op) {
  return op == Op::Call || op == Op::Param || isTerminator(op);
}

const char* opName(Op op) {
  switch (op) {
    case Op::Copy: return "copy";
    case Op::Add: return "add";
    case Op::Sub: return "sub";
    case Op::Mul: return "mul";
    case Op::Div: return "div";
    case Op::Mod: return "mod";
    case Op::Shl: return "shl";
    case Op::Neg: return "neg";
    case Op::Not: return "not";
    case Op::Lt: return "lt";
    case Op::Le: return "le";
    case Op::Gt: return "gt";
    case Op::Ge: return "ge";
    case Op::Eq: return "eq";
    case Op::Ne: return "ne";
    case Op::Param: return "param";
    case Op::Call: return "call";
    case Op::Jmp: return "jmp";
    case Op::Br: return "br";
    case Op::Ret: return "ret";
  }
  return "?";
}

int defOf(const Instr& in) { return in.dst; }

void usesOf(const Instr& in, std::vector<int>& out) {
  if (in.a.isReg()) out.push_back(static_cast<int>(in.a.v));
  if (in.b.isReg()) out.push_back(static_cast<int>(in.b.v));
  for (const auto& v : in.args)
    if (v.isReg()) out.push_back(static_cast<int>(v.v));
}

void computeCFG(Function& fn) {
  for (auto& b : fn.blocks) {
    b.succ.clear();
    b.pred.clear();
  }
  for (auto& b : fn.blocks) {
    if (b.dead || b.ins.empty()) continue;
    const Instr& t = b.ins.back();
    if (t.op == Op::Jmp) b.succ = {t.t1};
    if (t.op == Op::Br) b.succ = (t.t1 == t.t2) ? std::vector<int>{t.t1} : std::vector<int>{t.t1, t.t2};
  }
  for (auto& b : fn.blocks) {
    if (b.dead) continue;
    for (int s : b.succ) fn.blocks[s].pred.push_back(b.id);
  }
}

void compact(Function& fn) {
  // Keep live blocks, entry first, then in the order IR generation laid them out.
  std::vector<int> order;
  for (auto& b : fn.blocks)
    if (!b.dead) order.push_back(b.id);
  std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
    if (x == 0 || y == 0) return x == 0 && y != 0;
    return fn.blocks[x].seq < fn.blocks[y].seq;
  });

  std::vector<int> remap(fn.blocks.size(), -1);
  for (size_t i = 0; i < order.size(); ++i) remap[order[i]] = static_cast<int>(i);

  std::vector<Block> nb;
  nb.reserve(order.size());
  for (int old : order) {
    Block b = std::move(fn.blocks[old]);
    b.id = remap[old];
    for (auto& in : b.ins) {
      if (in.t1 >= 0) in.t1 = remap[in.t1];
      if (in.t2 >= 0) in.t2 = remap[in.t2];
    }
    nb.push_back(std::move(b));
  }
  fn.blocks = std::move(nb);
  computeCFG(fn);
}

int countInstrs(const Function& fn) {
  int n = 0;
  for (const auto& b : fn.blocks)
    if (!b.dead) n += static_cast<int>(b.ins.size());
  return n;
}

std::string valueStr(const Function& fn, const Value& v) {
  if (v.isImm()) return std::to_string(v.v);
  if (v.isReg()) {
    const auto& nm = fn.regNames[static_cast<size_t>(v.v)];
    return "%" + (nm.empty() ? "" : nm + ".") + std::to_string(v.v);
  }
  return "<none>";
}

void printFunction(const Function& fn, std::ostream& os) {
  os << "func " << fn.name << "(" << fn.nparams << " params) -> "
     << (fn.returnsValue ? "int" : "void") << " {\n";
  for (const auto& b : fn.blocks) {
    if (b.dead) continue;
    os << b.name << ":";
    if (!b.pred.empty()) {
      os << "                    ; preds:";
      for (int p : b.pred) os << " " << fn.blocks[p].name;
    }
    os << "\n";
    for (const auto& in : b.ins) {
      os << "    ";
      if (in.dst >= 0) os << valueStr(fn, Value::reg(in.dst)) << " = ";
      os << opName(in.op);
      switch (in.op) {
        case Op::Jmp:
          os << " " << fn.blocks[in.t1].name;
          break;
        case Op::Br:
          os << " " << valueStr(fn, in.a) << ", " << fn.blocks[in.t1].name << ", "
             << fn.blocks[in.t2].name;
          break;
        case Op::Call:
          os << " " << in.callee << "(";
          for (size_t i = 0; i < in.args.size(); ++i)
            os << (i ? ", " : "") << valueStr(fn, in.args[i]);
          os << ")";
          break;
        case Op::Param:
          os << " #" << in.a.v;
          break;
        default:
          if (in.a.kind != Value::None) os << " " << valueStr(fn, in.a);
          if (in.b.kind != Value::None) os << ", " << valueStr(fn, in.b);
      }
      os << "\n";
    }
  }
  os << "}\n";
}

void printModule(const Module& m, std::ostream& os) {
  for (size_t i = 0; i < m.funcs.size(); ++i) {
    if (i) os << "\n";
    printFunction(m.funcs[i], os);
  }
}
