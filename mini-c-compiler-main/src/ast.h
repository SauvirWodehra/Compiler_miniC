// ast.h - abstract syntax tree produced by the parser.
#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

enum class ExprKind { Num, Var, Binary, Unary, Assign, Call, IncDec };

struct Expr {
  ExprKind kind;
  int line = 0, col = 0;
  long long num = 0;        // Num
  std::string name;         // Var name / Call callee
  std::string op;           // Binary/Unary/Assign/IncDec operator text
  bool prefix = false;      // IncDec: ++x (true) vs x++ (false)
  std::unique_ptr<Expr> lhs, rhs;            // Binary, Assign(rhs), Unary(lhs)
  std::vector<std::unique_ptr<Expr>> args;   // Call
};

enum class StmtKind { Block, Decl, ExprStmt, If, While, For, Return, Break, Continue, Empty };

struct Stmt {
  StmtKind kind;
  int line = 0, col = 0;
  std::vector<std::unique_ptr<Stmt>> body;                                // Block
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> decls;       // Decl (init may be null)
  std::unique_ptr<Expr> expr;   // ExprStmt, Return (may be null)
  std::unique_ptr<Expr> cond;   // If, While, For (may be null for For)
  std::unique_ptr<Expr> step;   // For (may be null)
  std::unique_ptr<Stmt> init;   // For (Decl / ExprStmt / Empty)
  std::unique_ptr<Stmt> then;   // If then-branch, loop body
  std::unique_ptr<Stmt> els;    // If else-branch (may be null)
};

struct FuncDecl {
  std::string name;
  bool returnsValue = true;          // int vs void
  std::vector<std::string> params;   // names may be empty in prototypes
  std::unique_ptr<Stmt> body;        // null for a prototype
  int line = 0, col = 0;
};

struct Program {
  std::vector<FuncDecl> funcs;
};

void dumpAST(const Program& prog, std::ostream& os);
