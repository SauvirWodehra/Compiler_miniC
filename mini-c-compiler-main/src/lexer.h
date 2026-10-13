// lexer.h - turns Mini-C source text into a flat list of tokens.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

// Every stage reports errors through this exception so the driver can print
// "file:line:col: error: message" in one place.
struct CompileError : std::runtime_error {
  int line;
  int col;
  CompileError(const std::string& msg, int l, int c)
      : std::runtime_error(msg), line(l), col(c) {}
};

enum class Tok { Num, Ident, Keyword, Punct, Eof };

struct Token {
  Tok kind;
  std::string text;
  long long value = 0;  // only meaningful for Tok::Num
  int line = 0;
  int col = 0;
};

std::vector<Token> lex(const std::string& source);
const char* tokKindName(Tok kind);
