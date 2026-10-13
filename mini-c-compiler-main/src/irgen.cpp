// irgen.cpp - AST -> IR lowering.
//
// Control flow is lowered straight into basic blocks. Conditions in if/while/for
// go through genBranch(), which turns && / || / ! into jumps (short-circuit
// evaluation) instead of materialising 0/1 values.
#include "irgen.h"

#include <unordered_map>

#include "lexer.h"

namespace {

struct Signature {
  bool returnsValue;
  size_t nparams;
  bool defined;
};

const size_t kMaxParams = 6;  // System V passes the first 6 integer args in registers

class IRGen {
 public:
  explicit IRGen(Module& mod) : mod_(mod) {}

  void run(const Program& prog) {
    // Pass 1: collect every signature so calls may refer to functions defined later.
    for (const auto& f : prog.funcs) declare(f);
    // Pass 2: lower each function body.
    for (const auto& f : prog.funcs)
      if (f.body) genFunction(f);
  }

 private:
  Module& mod_;
  std::unordered_map<std::string, Signature> sigs_;
  Function* fn_ = nullptr;
  bool fnReturnsValue_ = true;
  int cur_ = 0;
  int seq_ = 0;
  std::vector<std::unordered_map<std::string, int>> scopes_;
  std::vector<std::pair<int, int>> loops_;  // (continue target, break target)

  // ---- helpers ------------------------------------------------------------
  void declare(const FuncDecl& f) {
    if (f.params.size() > kMaxParams)
      throw CompileError("functions with more than 6 parameters are not supported", f.line, f.col);
    auto it = sigs_.find(f.name);
    if (it == sigs_.end()) {
      sigs_[f.name] = {f.returnsValue, f.params.size(), f.body != nullptr};
      return;
    }
    if (it->second.returnsValue != f.returnsValue || it->second.nparams != f.params.size())
      throw CompileError("conflicting declaration of '" + f.name + "'", f.line, f.col);
    if (f.body) {
      if (it->second.defined) throw CompileError("redefinition of '" + f.name + "'", f.line, f.col);
      it->second.defined = true;
    }
  }

  int newReg(const std::string& name = "") {
    fn_->regNames.push_back(name);
    return fn_->nregs++;
  }

  int newBlock(const std::string& hint) {
    Block b;
    b.id = static_cast<int>(fn_->blocks.size());
    b.name = "bb" + std::to_string(b.id) + (hint.empty() ? "" : "." + hint);
    fn_->blocks.push_back(std::move(b));
    return fn_->blocks.back().id;
  }

  bool terminated() const {
    const auto& ins = fn_->blocks[cur_].ins;
    return !ins.empty() && isTerminator(ins.back().op);
  }

  // Makes `b` the insertion point, falling through from the current block.
  void startBlock(int b) {
    if (!terminated()) jump(b);
    cur_ = b;
    fn_->blocks[b].seq = ++seq_;
  }

  void emit(Instr in) {
    if (terminated()) {
      // Code after return/break/continue: put it in a fresh, unreachable block.
      // The optimizer deletes it; at -O0 it is simply never executed.
      int dead = newBlock("unreachable");
      cur_ = dead;
      fn_->blocks[dead].seq = ++seq_;
    }
    fn_->blocks[cur_].ins.push_back(std::move(in));
  }

  void jump(int target) {
    Instr in{Op::Jmp};
    in.t1 = target;
    emit(in);
  }

  void branch(Value cond, int t, int f) {
    Instr in{Op::Br};
    in.a = cond;
    in.t1 = t;
    in.t2 = f;
    emit(in);
  }

  Value binop(Op op, Value a, Value b) {
    Instr in{op};
    in.dst = newReg();
    in.a = a;
    in.b = b;
    emit(in);
    return Value::reg(in.dst);
  }

  void copy(int dst, Value src) {
    Instr in{Op::Copy};
    in.dst = dst;
    in.a = src;
    emit(in);
  }

  int lookup(const std::string& name, int line, int col) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto f = it->find(name);
      if (f != it->end()) return f->second;
    }
    throw CompileError("use of undeclared variable '" + name + "'", line, col);
  }

  int declareVar(const std::string& name, int line, int col) {
    auto& scope = scopes_.back();
    if (scope.count(name)) throw CompileError("redeclaration of '" + name + "'", line, col);
    int r = newReg(name);
    scope[name] = r;
    return r;
  }

  // ---- functions ----------------------------------------------------------
  void genFunction(const FuncDecl& f) {
    mod_.funcs.emplace_back();
    fn_ = &mod_.funcs.back();
    fn_->name = f.name;
    fn_->returnsValue = f.returnsValue;
    fn_->nparams = static_cast<int>(f.params.size());
    fnReturnsValue_ = f.returnsValue;
    seq_ = 0;
    cur_ = newBlock("entry");

    scopes_.clear();
    scopes_.emplace_back();
    for (size_t i = 0; i < f.params.size(); ++i) {
      Instr in{Op::Param};
      in.dst = declareVar(f.params[i], f.line, f.col);
      in.a = Value::imm(static_cast<int64_t>(i));
      emit(in);
    }

    genStmt(*f.body);

    // Falling off the end returns 0 (C guarantees this for main; harmless elsewhere).
    if (!terminated()) {
      Instr r{Op::Ret};
      if (fnReturnsValue_) r.a = Value::imm(0);
      emit(r);
    }
    compact(*fn_);
  }

  // ---- statements ---------------------------------------------------------
  void genStmt(const Stmt& s) {
    switch (s.kind) {
      case StmtKind::Block:
        scopes_.emplace_back();
        for (const auto& c : s.body) genStmt(*c);
        scopes_.pop_back();
        break;

      case StmtKind::Decl:
        for (const auto& d : s.decls) {
          // Evaluate the initializer before the name is in scope: int x = x; is an error.
          Value init = d.second ? genExpr(*d.second) : Value{};
          int r = declareVar(d.first, s.line, s.col);
          if (d.second) copy(r, init);
        }
        break;

      case StmtKind::ExprStmt:
        genExpr(*s.expr);
        break;

      case StmtKind::Empty:
        break;

      case StmtKind::Return: {
        Instr in{Op::Ret};
        if (s.expr) {
          if (!fnReturnsValue_)
            throw CompileError("void function cannot return a value", s.line, s.col);
          in.a = genExpr(*s.expr);
        } else if (fnReturnsValue_) {
          throw CompileError("non-void function must return a value", s.line, s.col);
        }
        emit(in);
        break;
      }

      case StmtKind::If: {
        int thenB = newBlock("if.then");
        int endB = newBlock("if.end");
        int elseB = s.els ? newBlock("if.else") : endB;
        genBranch(*s.cond, thenB, elseB);
        startBlock(thenB);
        genStmt(*s.then);
        if (s.els) {
          if (!terminated()) jump(endB);
          startBlock(elseB);
          genStmt(*s.els);
        }
        startBlock(endB);
        break;
      }

      case StmtKind::While: {
        int condB = newBlock("while.cond");
        int bodyB = newBlock("while.body");
        int endB = newBlock("while.end");
        startBlock(condB);
        genBranch(*s.cond, bodyB, endB);
        startBlock(bodyB);
        loops_.emplace_back(condB, endB);
        genStmt(*s.then);
        loops_.pop_back();
        if (!terminated()) jump(condB);
        startBlock(endB);
        break;
      }

      case StmtKind::For: {
        scopes_.emplace_back();  // for (int i = ...) scopes i to the loop
        genStmt(*s.init);
        int condB = newBlock("for.cond");
        int bodyB = newBlock("for.body");
        int stepB = newBlock("for.step");
        int endB = newBlock("for.end");
        startBlock(condB);
        if (s.cond) genBranch(*s.cond, bodyB, endB);
        else jump(bodyB);
        startBlock(bodyB);
        loops_.emplace_back(stepB, endB);
        genStmt(*s.then);
        loops_.pop_back();
        startBlock(stepB);
        if (s.step) genExpr(*s.step);
        jump(condB);
        startBlock(endB);
        scopes_.pop_back();
        break;
      }

      case StmtKind::Break:
      case StmtKind::Continue:
        if (loops_.empty())
          throw CompileError(std::string(s.kind == StmtKind::Break ? "break" : "continue") +
                                 " statement not within a loop",
                             s.line, s.col);
        jump(s.kind == StmtKind::Break ? loops_.back().second : loops_.back().first);
        break;
    }
  }

  // Emits a conditional jump on `e` without materialising a 0/1 value
  // for the logical operators (short-circuit evaluation).
  void genBranch(const Expr& e, int t, int f) {
    if (e.kind == ExprKind::Binary && e.op == "&&") {
      int mid = newBlock("and.rhs");
      genBranch(*e.lhs, mid, f);
      startBlock(mid);
      genBranch(*e.rhs, t, f);
      return;
    }
    if (e.kind == ExprKind::Binary && e.op == "||") {
      int mid = newBlock("or.rhs");
      genBranch(*e.lhs, t, mid);
      startBlock(mid);
      genBranch(*e.rhs, t, f);
      return;
    }
    if (e.kind == ExprKind::Unary && e.op == "!") {
      genBranch(*e.lhs, f, t);
      return;
    }
    branch(genExpr(e), t, f);
  }

  // ---- expressions --------------------------------------------------------
  static Op binaryOp(const std::string& op) {
    if (op == "+") return Op::Add;
    if (op == "-") return Op::Sub;
    if (op == "*") return Op::Mul;
    if (op == "/") return Op::Div;
    if (op == "%") return Op::Mod;
    if (op == "<") return Op::Lt;
    if (op == "<=") return Op::Le;
    if (op == ">") return Op::Gt;
    if (op == ">=") return Op::Ge;
    if (op == "==") return Op::Eq;
    return Op::Ne;  // "!="
  }

  Value genExpr(const Expr& e) {
    switch (e.kind) {
      case ExprKind::Num:
        return Value::imm(e.num);

      case ExprKind::Var:
        return Value::reg(lookup(e.name, e.line, e.col));

      case ExprKind::Unary: {
        Value v = genExpr(*e.lhs);
        Instr in{e.op == "-" ? Op::Neg : Op::Not};
        in.dst = newReg();
        in.a = v;
        emit(in);
        return Value::reg(in.dst);
      }

      case ExprKind::Binary: {
        if (e.op == "&&" || e.op == "||") {
          // Value context: r = cond ? 1 : 0 via control flow.
          int r = newReg();
          int tB = newBlock("bool.true");
          int fB = newBlock("bool.false");
          int endB = newBlock("bool.end");
          genBranch(e, tB, fB);
          startBlock(tB);
          copy(r, Value::imm(1));
          jump(endB);
          startBlock(fB);
          copy(r, Value::imm(0));
          startBlock(endB);
          return Value::reg(r);
        }
        Value a = genExpr(*e.lhs);
        // A plain variable operand must be snapshotted if the right side could
        // modify it (e.g. x + (x = 5)) - keep evaluation left-to-right.
        if (a.isReg() && e.lhs->kind == ExprKind::Var && hasAssignment(*e.rhs)) {
          int t = newReg();
          copy(t, a);
          a = Value::reg(t);
        }
        Value b = genExpr(*e.rhs);
        return binop(binaryOp(e.op), a, b);
      }

      case ExprKind::Assign: {
        int var = lookup(e.name, e.line, e.col);
        Value rhs = genExpr(*e.rhs);
        if (e.op == "=") {
          copy(var, rhs);
        } else {
          Op op = binaryOp(e.op.substr(0, 1));
          Instr in{op};
          in.dst = var;
          in.a = Value::reg(var);
          in.b = rhs;
          emit(in);
        }
        return Value::reg(var);
      }

      case ExprKind::IncDec: {
        int var = lookup(e.name, e.line, e.col);
        Op op = e.op == "++" ? Op::Add : Op::Sub;
        Value old;
        if (!e.prefix) {  // x++ yields the old value
          int t = newReg();
          copy(t, Value::reg(var));
          old = Value::reg(t);
        }
        Instr in{op};
        in.dst = var;
        in.a = Value::reg(var);
        in.b = Value::imm(1);
        emit(in);
        return e.prefix ? Value::reg(var) : old;
      }

      case ExprKind::Call: {
        auto it = sigs_.find(e.name);
        if (it == sigs_.end())
          throw CompileError("call to undeclared function '" + e.name + "'", e.line, e.col);
        if (it->second.nparams != e.args.size())
          throw CompileError("function '" + e.name + "' expects " +
                                 std::to_string(it->second.nparams) + " argument(s), got " +
                                 std::to_string(e.args.size()),
                             e.line, e.col);
        Instr in{Op::Call};
        in.callee = e.name;
        for (size_t i = 0; i < e.args.size(); ++i) {
          const Expr& a = *e.args[i];
          Value v = genExpr(a);
          // Snapshot a variable if a later argument assigns to something.
          bool laterWrites = false;
          for (size_t j = i + 1; j < e.args.size(); ++j) laterWrites |= hasAssignment(*e.args[j]);
          if (v.isReg() && a.kind == ExprKind::Var && laterWrites) {
            int t = newReg();
            copy(t, v);
            v = Value::reg(t);
          }
          in.args.push_back(v);
        }
        in.dst = it->second.returnsValue ? newReg() : -1;
        emit(in);
        return in.dst >= 0 ? Value::reg(in.dst) : Value::imm(0);
      }
    }
    return Value::imm(0);
  }

  static bool hasAssignment(const Expr& e) {
    // Calls cannot touch our locals (no pointers/globals), so only these count.
    if (e.kind == ExprKind::Assign || e.kind == ExprKind::IncDec) return true;
    if (e.lhs && hasAssignment(*e.lhs)) return true;
    if (e.rhs && hasAssignment(*e.rhs)) return true;
    for (const auto& a : e.args)
      if (hasAssignment(*a)) return true;
    return false;
  }
};

}  // namespace

Module generateIR(const Program& prog) {
  Module m;
  m.funcs.reserve(prog.funcs.size());  // keeps Function* stable while generating
  IRGen(m).run(prog);
  return m;
}
