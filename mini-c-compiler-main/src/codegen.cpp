// codegen.cpp - instruction selection for x86-64.
//
// Register conventions used by the generated code:
//   %ebx %r12d %r13d %r14d %r15d   allocatable (callee-saved, survive calls)
//   %eax %ecx %edx %r11d           scratch, never hold a virtual register
//   %edi %esi %edx %ecx %r8d %r9d  argument registers for calls
// All arithmetic is 32-bit to match C's `int` (wrap-around on overflow).
//
// Stack frame:
//   [rbp+8]  return address
//   [rbp]    saved rbp
//   [rbp-8 ...]          saved callee-saved registers (pushq)
//   below them          spill slots, 8 bytes each
// The frame size is padded so %rsp is 16-byte aligned at every call.
#include "codegen.h"

#include <string>
#include <unordered_map>

namespace {

const char* const kReg32[] = {"%ebx", "%r12d", "%r13d", "%r14d", "%r15d"};
const char* const kReg64[] = {"%rbx", "%r12", "%r13", "%r14", "%r15"};
const char* const kArg32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};

const char* condSuffix(Op op) {
  switch (op) {
    case Op::Lt: return "l";
    case Op::Le: return "le";
    case Op::Gt: return "g";
    case Op::Ge: return "ge";
    case Op::Eq: return "e";
    default: return "ne";
  }
}

const char* invertedSuffix(Op op) {
  switch (op) {
    case Op::Lt: return "ge";
    case Op::Le: return "g";
    case Op::Gt: return "le";
    case Op::Ge: return "l";
    case Op::Eq: return "ne";
    default: return "e";
  }
}

// k if v == 2^k for 1 <= k <= 30, else -1.
int log2Exact(int64_t v) {
  if (v < 2 || v > (int64_t{1} << 30) || (v & (v - 1)) != 0) return -1;
  int k = 0;
  while ((int64_t{1} << k) != v) ++k;
  return k;
}

class FunctionEmitter {
 public:
  FunctionEmitter(const Function& fn, const Allocation& alloc, const CodegenOptions& opts,
                  std::ostream& os)
      : fn_(fn), alloc_(alloc), opts_(opts), os_(os) {
    savedBytes_ = static_cast<int>(alloc.usedRegs.size()) * 8;
    countUsesAndDefs();
  }

  void run() {
    os_ << "\n    .globl " << fn_.name << "\n    .type " << fn_.name << ", @function\n"
        << fn_.name << ":\n";
    prologue();
    for (size_t bi = 0; bi < fn_.blocks.size(); ++bi) {
      const Block& b = fn_.blocks[bi];
      if (b.dead) continue;
      os_ << label(b.id) << ":    # " << b.name << "\n";
      for (size_t i = 0; i < b.ins.size(); ++i) {
        // Compare feeding the very next branch and nothing else: fuse them.
        if (opts_.fuseCompareBranch && i + 1 < b.ins.size() && canFuse(b.ins[i], b.ins[i + 1])) {
          emitFusedBranch(b.ins[i], b.ins[i + 1], static_cast<int>(bi));
          ++i;
          continue;
        }
        emitInstr(b.ins[i], static_cast<int>(bi));
      }
    }
    epilogue();
    os_ << "    .size " << fn_.name << ", .-" << fn_.name << "\n";
  }

 private:
  const Function& fn_;
  const Allocation& alloc_;
  const CodegenOptions& opts_;
  std::ostream& os_;
  int savedBytes_ = 0;
  std::vector<int> useCount_, defCount_;

  void countUsesAndDefs() {
    useCount_.assign(fn_.nregs, 0);
    defCount_.assign(fn_.nregs, 0);
    std::vector<int> tmp;
    for (const auto& b : fn_.blocks) {
      if (b.dead) continue;
      for (const auto& in : b.ins) {
        tmp.clear();
        usesOf(in, tmp);
        for (int r : tmp) ++useCount_[r];
        if (in.dst >= 0) ++defCount_[in.dst];
      }
    }
  }

  std::string label(int block) const {
    return ".L" + fn_.name + "_" + std::to_string(block);
  }
  std::string retLabel() const { return ".L" + fn_.name + "_ret"; }

  bool inReg(int vreg) const { return alloc_.loc[vreg].kind == Location::Reg; }

  // Operand text for a virtual register's home: a register or a stack slot.
  std::string home(int vreg) const {
    const Location& l = alloc_.loc[vreg];
    if (l.kind == Location::Reg) return kReg32[l.index];
    int off = savedBytes_ + 8 * (l.index + 1);
    return "-" + std::to_string(off) + "(%rbp)";
  }

  std::string operand(const Value& v) const {
    if (v.isImm()) return "$" + std::to_string(v.v);
    return home(static_cast<int>(v.v));
  }

  static bool isMem(const std::string& s) { return s.find('(') != std::string::npos; }

  void ins(const std::string& text) { os_ << "    " << text << "\n"; }

  // mov that tolerates memory-to-memory by bouncing through %r11d.
  void move(const std::string& src, const std::string& dst) {
    if (src == dst) return;
    if (isMem(src) && isMem(dst)) {
      ins("movl " + src + ", %r11d");
      ins("movl %r11d, " + dst);
    } else {
      ins("movl " + src + ", " + dst);
    }
  }

  void prologue() {
    ins("pushq %rbp");
    ins("movq %rsp, %rbp");
    for (int r : alloc_.usedRegs) ins(std::string("pushq ") + kReg64[r]);
    int frame = alloc_.numSlots * 8;
    if ((savedBytes_ + frame) % 16 != 0) frame += 8;  // keep %rsp 16-byte aligned
    if (frame > 0) ins("subq $" + std::to_string(frame) + ", %rsp");
  }

  void epilogue() {
    os_ << retLabel() << ":\n";
    if (savedBytes_ > 0) ins("leaq -" + std::to_string(savedBytes_) + "(%rbp), %rsp");
    else ins("movq %rbp, %rsp");
    for (size_t i = alloc_.usedRegs.size(); i-- > 0;)
      ins(std::string("popq ") + kReg64[alloc_.usedRegs[i]]);
    ins("popq %rbp");
    ins("ret");
  }

  bool isNextBlock(int cur, int target) const {
    for (size_t k = cur + 1; k < fn_.blocks.size(); ++k) {
      if (fn_.blocks[k].dead) continue;
      return static_cast<int>(k) == target;
    }
    return false;
  }

  void jumpTo(int target, int cur) {
    if (!isNextBlock(cur, target)) ins("jmp " + label(target));
  }

  // Emits "if (cond) goto t else goto f" given the jcc suffixes for true/false.
  void condJump(const char* trueCc, const char* falseCc, int t, int f, int cur) {
    if (isNextBlock(cur, t)) {
      ins(std::string("j") + falseCc + " " + label(f));
    } else {
      ins(std::string("j") + trueCc + " " + label(t));
      jumpTo(f, cur);
    }
  }

  bool canFuse(const Instr& cmp, const Instr& br) const {
    return isCompare(cmp.op) && br.op == Op::Br && br.a.isReg() && br.a.v == cmp.dst &&
           useCount_[cmp.dst] == 1 && defCount_[cmp.dst] == 1;
  }

  // Puts `a` somewhere usable as the left operand of cmp (a register).
  std::string cmpLeft(const Value& a) {
    if (a.isReg() && inReg(static_cast<int>(a.v))) return home(static_cast<int>(a.v));
    move(operand(a), "%eax");
    return "%eax";
  }

  void emitFusedBranch(const Instr& cmp, const Instr& br, int cur) {
    std::string left = cmpLeft(cmp.a);
    ins("cmpl " + operand(cmp.b) + ", " + left);
    condJump(condSuffix(cmp.op), invertedSuffix(cmp.op), br.t1, br.t2, cur);
  }

  // Register to compute into: the destination itself when it is a register and
  // does not alias the second operand, otherwise the scratch %eax.
  std::string workReg(const Instr& in) const {
    if (inReg(in.dst)) {
      std::string d = home(in.dst);
      if (!(in.b.isReg() && operand(in.b) == d)) return d;
    }
    return "%eax";
  }

  void emitInstr(const Instr& in, int cur) {
    switch (in.op) {
      case Op::Copy:
        move(operand(in.a), home(in.dst));
        break;

      case Op::Param:
        move(kArg32[in.a.v], home(in.dst));
        break;

      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Shl: {
        std::string w = workReg(in);
        move(operand(in.a), w);
        const char* mn = in.op == Op::Add ? "addl" : in.op == Op::Sub ? "subl"
                       : in.op == Op::Mul ? "imull" : "shll";
        ins(std::string(mn) + " " + operand(in.b) + ", " + w);
        move(w, home(in.dst));
        break;
      }

      case Op::Div:
      case Op::Mod: {
        int k = in.b.isImm() ? log2Exact(in.b.v) : -1;
        if (k >= 1) {
          // Signed x / 2^k without idiv. An arithmetic shift rounds toward
          // -infinity but C truncates toward zero, so negative x first gets a
          // bias of 2^k - 1:  bias = (x >> 31) >>> (32 - k).
          move(operand(in.a), "%eax");
          ins("movl %eax, %edx");
          if (k > 1) ins("sarl $31, %edx");
          ins("shrl $" + std::to_string(32 - k) + ", %edx");
          if (in.op == Op::Div) {
            ins("addl %edx, %eax");
            ins("sarl $" + std::to_string(k) + ", %eax");
          } else {
            // x % 2^k = x - ((x + bias) & -2^k)
            ins("movl %eax, %ecx");
            ins("addl %edx, %ecx");
            ins("andl $" + std::to_string(-(int64_t{1} << k)) + ", %ecx");
            ins("subl %ecx, %eax");
          }
          move("%eax", home(in.dst));
          break;
        }
        move(operand(in.a), "%eax");
        ins("cltd");  // sign-extend eax into edx:eax
        std::string divisor = operand(in.b);
        if (in.b.isImm()) {
          ins("movl " + divisor + ", %ecx");
          divisor = "%ecx";
        }
        ins("idivl " + divisor);
        move(in.op == Op::Div ? "%eax" : "%edx", home(in.dst));
        break;
      }

      case Op::Neg: {
        std::string w = inReg(in.dst) ? home(in.dst) : "%eax";
        move(operand(in.a), w);
        ins("negl " + w);
        move(w, home(in.dst));
        break;
      }

      case Op::Not:
        move(operand(in.a), "%eax");
        ins("cmpl $0, %eax");
        ins("sete %al");
        ins("movzbl %al, %eax");
        move("%eax", home(in.dst));
        break;

      case Op::Lt:
      case Op::Le:
      case Op::Gt:
      case Op::Ge:
      case Op::Eq:
      case Op::Ne: {
        std::string left = cmpLeft(in.a);
        ins("cmpl " + operand(in.b) + ", " + left);
        ins(std::string("set") + condSuffix(in.op) + " %al");
        ins("movzbl %al, %eax");
        move("%eax", home(in.dst));
        break;
      }

      case Op::Call:
        // Sources are callee-saved regs, stack slots or immediates, so loading
        // argument registers in order can never clobber a later source.
        for (size_t i = 0; i < in.args.size(); ++i) move(operand(in.args[i]), kArg32[i]);
        ins("call " + in.callee);
        if (in.dst >= 0) move("%eax", home(in.dst));
        break;

      case Op::Jmp:
        jumpTo(in.t1, cur);
        break;

      case Op::Br:
        if (in.a.isImm()) {
          jumpTo(in.a.v != 0 ? in.t1 : in.t2, cur);
        } else {
          ins("cmpl $0, " + operand(in.a));
          condJump("ne", "e", in.t1, in.t2, cur);
        }
        break;

      case Op::Ret: {
        if (in.a.kind != Value::None) move(operand(in.a), "%eax");
        // The epilogue directly follows the last block, so skip a jump there.
        bool last = true;
        for (size_t k = cur + 1; k < fn_.blocks.size(); ++k)
          if (!fn_.blocks[k].dead) last = false;
        if (!last) ins("jmp " + retLabel());
        break;
      }
    }
  }
};

}  // namespace

void emitPrologueFile(std::ostream& os) { os << "    .text\n"; }

void emitEpilogueFile(std::ostream& os) {
  os << "\n    .section .note.GNU-stack,\"\",@progbits\n";
}

void emitFunction(const Function& fn, const Allocation& alloc, const CodegenOptions& opts,
                  std::ostream& os) {
  FunctionEmitter(fn, alloc, opts, os).run();
}
