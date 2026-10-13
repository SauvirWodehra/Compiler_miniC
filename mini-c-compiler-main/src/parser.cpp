// parser.cpp - recursive descent with one function per precedence level.
//
// Grammar (lowest to highest precedence):
//   expr       := assign
//   assign     := logor ( ("=" | "+=" | "-=" | "*=" | "/=" | "%=") assign )?
//   logor      := logand ( "||" logand )*
//   logand     := equality ( "&&" equality )*
//   equality   := relational ( ("==" | "!=") relational )*
//   relational := additive ( ("<" | "<=" | ">" | ">=") additive )*
//   additive   := mult ( ("+" | "-") mult )*
//   mult       := unary ( ("*" | "/" | "%") unary )*
//   unary      := ("-" | "+" | "!" | "++" | "--") unary | postfix
//   postfix    := primary ( "++" | "--" )?
//   primary    := NUM | IDENT | IDENT "(" args ")" | "(" expr ")"
#include "parser.h"

#include <ostream>

namespace {

// Binary operator table, one row per precedence level (index 0 = loosest).
const std::vector<std::vector<std::string>> kLevels = {
    {"||"}, {"&&"}, {"==", "!="}, {"<", "<=", ">", ">="}, {"+", "-"}, {"*", "/", "%"}};

std::unique_ptr<Expr> makeExpr(ExprKind k, const Token& at) {
  auto e = std::make_unique<Expr>();
  e->kind = k;
  e->line = at.line;
  e->col = at.col;
  return e;
}

std::unique_ptr<Stmt> makeStmt(StmtKind k, const Token& at) {
  auto s = std::make_unique<Stmt>();
  s->kind = k;
  s->line = at.line;
  s->col = at.col;
  return s;
}

}  // namespace

const Token& Parser::peek(size_t k) const {
  size_t i = pos_ + k;
  return i < toks_.size() ? toks_[i] : toks_.back();
}

const Token& Parser::next() {
  const Token& t = peek();
  if (pos_ < toks_.size() - 1) ++pos_;
  return t;
}

bool Parser::isPunct(const char* p, size_t k) const {
  return peek(k).kind == Tok::Punct && peek(k).text == p;
}

bool Parser::isKeyword(const char* kw, size_t k) const {
  return peek(k).kind == Tok::Keyword && peek(k).text == kw;
}

bool Parser::acceptPunct(const char* p) {
  if (!isPunct(p)) return false;
  next();
  return true;
}

void Parser::fail(const std::string& msg) const {
  throw CompileError(msg + " (found '" + peek().text + "')", peek().line, peek().col);
}

const Token& Parser::expectPunct(const char* p) {
  if (!isPunct(p)) fail(std::string("expected '") + p + "'");
  return next();
}

const Token& Parser::expectIdent() {
  if (peek().kind != Tok::Ident) fail("expected identifier");
  return next();
}

Program Parser::parseProgram() {
  Program prog;
  while (peek().kind != Tok::Eof) prog.funcs.push_back(parseFunction());
  return prog;
}

FuncDecl Parser::parseFunction() {
  FuncDecl f;
  if (isKeyword("int")) {
    f.returnsValue = true;
  } else if (isKeyword("void")) {
    f.returnsValue = false;
  } else {
    fail("expected function return type 'int' or 'void'");
  }
  next();
  const Token& name = expectIdent();
  f.name = name.text;
  f.line = name.line;
  f.col = name.col;

  expectPunct("(");
  if (isKeyword("void") && isPunct(")", 1)) {
    next();  // f(void)
  } else if (!isPunct(")")) {
    do {
      if (!isKeyword("int")) fail("expected parameter type 'int'");
      next();
      // Prototypes may omit parameter names: int print(int);
      f.params.push_back(peek().kind == Tok::Ident ? next().text : "");
    } while (acceptPunct(","));
  }
  expectPunct(")");

  if (acceptPunct(";")) return f;  // prototype only

  for (const auto& p : f.params)
    if (p.empty()) fail("parameter name omitted in function definition");
  f.body = parseBlock();
  return f;
}

std::unique_ptr<Stmt> Parser::parseBlock() {
  auto s = makeStmt(StmtKind::Block, expectPunct("{"));
  while (!isPunct("}")) {
    if (peek().kind == Tok::Eof) fail("expected '}' before end of file");
    s->body.push_back(parseStmt());
  }
  next();
  return s;
}

std::unique_ptr<Stmt> Parser::parseDecl() {
  auto s = makeStmt(StmtKind::Decl, next());  // consume 'int'
  do {
    const Token& name = expectIdent();
    std::unique_ptr<Expr> init;
    if (acceptPunct("=")) init = parseAssign();
    s->decls.emplace_back(name.text, std::move(init));
  } while (acceptPunct(","));
  expectPunct(";");
  return s;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  const Token& t = peek();

  if (isPunct("{")) return parseBlock();
  if (isKeyword("int")) return parseDecl();
  if (acceptPunct(";")) return makeStmt(StmtKind::Empty, t);

  if (isKeyword("return")) {
    auto s = makeStmt(StmtKind::Return, next());
    if (!isPunct(";")) s->expr = parseExpr();
    expectPunct(";");
    return s;
  }
  if (isKeyword("break") || isKeyword("continue")) {
    auto s = makeStmt(t.text == "break" ? StmtKind::Break : StmtKind::Continue, next());
    expectPunct(";");
    return s;
  }
  if (isKeyword("if")) {
    auto s = makeStmt(StmtKind::If, next());
    expectPunct("(");
    s->cond = parseExpr();
    expectPunct(")");
    s->then = parseStmt();
    if (isKeyword("else")) {
      next();
      s->els = parseStmt();
    }
    return s;
  }
  if (isKeyword("while")) {
    auto s = makeStmt(StmtKind::While, next());
    expectPunct("(");
    s->cond = parseExpr();
    expectPunct(")");
    s->then = parseStmt();
    return s;
  }
  if (isKeyword("for")) {
    auto s = makeStmt(StmtKind::For, next());
    expectPunct("(");
    if (isKeyword("int")) {
      s->init = parseDecl();  // consumes ';'
    } else if (isPunct(";")) {
      s->init = makeStmt(StmtKind::Empty, next());
    } else {
      s->init = makeStmt(StmtKind::ExprStmt, peek());
      s->init->expr = parseExpr();
      expectPunct(";");
    }
    if (!isPunct(";")) s->cond = parseExpr();
    expectPunct(";");
    if (!isPunct(")")) s->step = parseExpr();
    expectPunct(")");
    s->then = parseStmt();
    return s;
  }

  auto s = makeStmt(StmtKind::ExprStmt, t);
  s->expr = parseExpr();
  expectPunct(";");
  return s;
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseAssign(); }

std::unique_ptr<Expr> Parser::parseAssign() {
  auto lhs = parseBinaryLevel(0);
  static const char* const kAssignOps[] = {"=", "+=", "-=", "*=", "/=", "%="};
  for (const char* op : kAssignOps) {
    if (isPunct(op)) {
      const Token& t = next();
      if (lhs->kind != ExprKind::Var)
        throw CompileError("left side of assignment must be a variable", t.line, t.col);
      auto e = makeExpr(ExprKind::Assign, t);
      e->op = op;
      e->name = lhs->name;
      e->rhs = parseAssign();  // right-associative: a = b = c
      return e;
    }
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseBinaryLevel(int level) {
  if (level == static_cast<int>(kLevels.size())) return parseUnary();
  auto lhs = parseBinaryLevel(level + 1);
  for (;;) {
    bool matched = false;
    for (const auto& op : kLevels[level]) {
      if (isPunct(op.c_str())) {
        auto e = makeExpr(ExprKind::Binary, next());
        e->op = op;
        e->lhs = std::move(lhs);
        e->rhs = parseBinaryLevel(level + 1);  // left-associative
        lhs = std::move(e);
        matched = true;
        break;
      }
    }
    if (!matched) return lhs;
  }
}

std::unique_ptr<Expr> Parser::parseUnary() {
  if (isPunct("++") || isPunct("--")) {
    const Token& t = next();
    auto operand = parseUnary();
    if (operand->kind != ExprKind::Var)
      throw CompileError("operand of " + t.text + " must be a variable", t.line, t.col);
    auto e = makeExpr(ExprKind::IncDec, t);
    e->op = t.text;
    e->prefix = true;
    e->name = operand->name;
    return e;
  }
  if (isPunct("+")) {
    next();
    return parseUnary();
  }
  if (isPunct("-") || isPunct("!")) {
    auto e = makeExpr(ExprKind::Unary, peek());
    e->op = next().text;
    e->lhs = parseUnary();
    return e;
  }
  return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
  auto e = parsePrimary();
  if (isPunct("++") || isPunct("--")) {
    const Token& t = next();
    if (e->kind != ExprKind::Var)
      throw CompileError("operand of " + t.text + " must be a variable", t.line, t.col);
    auto inc = makeExpr(ExprKind::IncDec, t);
    inc->op = t.text;
    inc->prefix = false;
    inc->name = e->name;
    return inc;
  }
  return e;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
  const Token& t = peek();
  if (t.kind == Tok::Num) {
    auto e = makeExpr(ExprKind::Num, next());
    e->num = t.value;
    return e;
  }
  if (t.kind == Tok::Ident) {
    next();
    if (acceptPunct("(")) {
      auto e = makeExpr(ExprKind::Call, t);
      e->name = t.text;
      if (!isPunct(")")) {
        do e->args.push_back(parseAssign());
        while (acceptPunct(","));
      }
      expectPunct(")");
      return e;
    }
    auto e = makeExpr(ExprKind::Var, t);
    e->name = t.text;
    return e;
  }
  if (acceptPunct("(")) {
    auto e = parseExpr();
    expectPunct(")");
    return e;
  }
  fail("expected expression");
}

// ---------------------------------------------------------------------------
// AST dump (--emit-ast)
// ---------------------------------------------------------------------------
namespace {

void dumpExpr(const Expr& e, std::ostream& os) {
  switch (e.kind) {
    case ExprKind::Num: os << e.num; break;
    case ExprKind::Var: os << e.name; break;
    case ExprKind::Binary:
      os << "(" << e.op << " ";
      dumpExpr(*e.lhs, os);
      os << " ";
      dumpExpr(*e.rhs, os);
      os << ")";
      break;
    case ExprKind::Unary:
      os << "(" << e.op << " ";
      dumpExpr(*e.lhs, os);
      os << ")";
      break;
    case ExprKind::Assign:
      os << "(" << e.op << " " << e.name << " ";
      dumpExpr(*e.rhs, os);
      os << ")";
      break;
    case ExprKind::IncDec:
      os << "(" << (e.prefix ? "pre" : "post") << e.op << " " << e.name << ")";
      break;
    case ExprKind::Call:
      os << "(call " << e.name;
      for (const auto& a : e.args) {
        os << " ";
        dumpExpr(*a, os);
      }
      os << ")";
      break;
  }
}

void dumpStmt(const Stmt& s, std::ostream& os, int depth) {
  const std::string pad(depth * 2, ' ');
  switch (s.kind) {
    case StmtKind::Block:
      os << pad << "block\n";
      for (const auto& c : s.body) dumpStmt(*c, os, depth + 1);
      break;
    case StmtKind::Decl:
      for (const auto& d : s.decls) {
        os << pad << "decl " << d.first;
        if (d.second) {
          os << " = ";
          dumpExpr(*d.second, os);
        }
        os << "\n";
      }
      break;
    case StmtKind::ExprStmt:
      os << pad << "expr ";
      dumpExpr(*s.expr, os);
      os << "\n";
      break;
    case StmtKind::If:
      os << pad << "if ";
      dumpExpr(*s.cond, os);
      os << "\n";
      dumpStmt(*s.then, os, depth + 1);
      if (s.els) {
        os << pad << "else\n";
        dumpStmt(*s.els, os, depth + 1);
      }
      break;
    case StmtKind::While:
      os << pad << "while ";
      dumpExpr(*s.cond, os);
      os << "\n";
      dumpStmt(*s.then, os, depth + 1);
      break;
    case StmtKind::For:
      os << pad << "for\n";
      dumpStmt(*s.init, os, depth + 1);
      os << pad << "  cond ";
      if (s.cond) dumpExpr(*s.cond, os);
      os << "\n" << pad << "  step ";
      if (s.step) dumpExpr(*s.step, os);
      os << "\n";
      dumpStmt(*s.then, os, depth + 1);
      break;
    case StmtKind::Return:
      os << pad << "return";
      if (s.expr) {
        os << " ";
        dumpExpr(*s.expr, os);
      }
      os << "\n";
      break;
    case StmtKind::Break: os << pad << "break\n"; break;
    case StmtKind::Continue: os << pad << "continue\n"; break;
    case StmtKind::Empty: os << pad << "empty\n"; break;
  }
}

}  // namespace

void dumpAST(const Program& prog, std::ostream& os) {
  for (const auto& f : prog.funcs) {
    os << (f.returnsValue ? "int " : "void ") << f.name << "(";
    for (size_t i = 0; i < f.params.size(); ++i) os << (i ? ", " : "") << f.params[i];
    os << ")" << (f.body ? "\n" : " ;prototype\n");
    if (f.body) dumpStmt(*f.body, os, 1);
  }
}
