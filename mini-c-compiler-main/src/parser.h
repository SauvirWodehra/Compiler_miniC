// parser.h - recursive-descent parser: tokens -> AST.
#pragma once

#include <vector>

#include "ast.h"
#include "lexer.h"

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}
  Program parseProgram();

 private:
  std::vector<Token> toks_;
  size_t pos_ = 0;

  const Token& peek(size_t k = 0) const;
  const Token& next();
  bool isPunct(const char* p, size_t k = 0) const;
  bool isKeyword(const char* kw, size_t k = 0) const;
  bool acceptPunct(const char* p);
  const Token& expectPunct(const char* p);
  const Token& expectIdent();
  [[noreturn]] void fail(const std::string& msg) const;

  FuncDecl parseFunction();
  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<Stmt> parseBlock();
  std::unique_ptr<Stmt> parseDecl();

  std::unique_ptr<Expr> parseExpr();
  std::unique_ptr<Expr> parseAssign();
  std::unique_ptr<Expr> parseBinaryLevel(int level);
  std::unique_ptr<Expr> parseUnary();
  std::unique_ptr<Expr> parsePostfix();
  std::unique_ptr<Expr> parsePrimary();
};
