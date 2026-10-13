// lexer.cpp - hand-written scanner (no regex, no generator).
#include "lexer.h"

#include <cctype>
#include <climits>
#include <unordered_set>

namespace {

const std::unordered_set<std::string> kKeywords = {
    "int", "void", "return", "if", "else", "while", "for", "break", "continue"};

// Ordered so that two-character operators are tried before their prefixes
// (maximal munch): "<=" must win over "<", "++" over "+", etc.
const char* const kPuncts[] = {"&&", "||", "==", "!=", "<=", ">=", "+=", "-=", "*=",
                               "/=", "%=", "++", "--", "+",  "-",  "*",  "/",  "%",
                               "<",  ">",  "=",  "!",  "(",  ")",  "{",  "}",  ";",
                               ","};

}  // namespace

const char* tokKindName(Tok kind) {
  switch (kind) {
    case Tok::Num: return "NUM";
    case Tok::Ident: return "IDENT";
    case Tok::Keyword: return "KEYWORD";
    case Tok::Punct: return "PUNCT";
    case Tok::Eof: return "EOF";
  }
  return "?";
}

std::vector<Token> lex(const std::string& s) {
  std::vector<Token> out;
  size_t i = 0;
  int line = 1, col = 1;

  auto advance = [&](size_t n) {
    for (size_t k = 0; k < n && i < s.size(); ++k, ++i) {
      if (s[i] == '\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
    }
  };

  while (i < s.size()) {
    const char c = s[i];

    if (std::isspace(static_cast<unsigned char>(c))) {
      advance(1);
      continue;
    }
    // Preprocessor lines (#include ...) are ignored so test files stay valid C.
    if (c == '#') {
      while (i < s.size() && s[i] != '\n') advance(1);
      continue;
    }
    if (s.compare(i, 2, "//") == 0) {
      while (i < s.size() && s[i] != '\n') advance(1);
      continue;
    }
    if (s.compare(i, 2, "/*") == 0) {
      const int l = line, cc = col;
      advance(2);
      while (i < s.size() && s.compare(i, 2, "*/") != 0) advance(1);
      if (i >= s.size()) throw CompileError("unterminated block comment", l, cc);
      advance(2);
      continue;
    }

    Token t;
    t.line = line;
    t.col = col;

    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t j = i;
      long long v = 0;
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
        v = v * 10 + (s[j] - '0');
        if (v > INT_MAX) throw CompileError("integer literal too large for int", line, col);
        ++j;
      }
      if (j < s.size() && (std::isalpha(static_cast<unsigned char>(s[j])) || s[j] == '_'))
        throw CompileError("invalid suffix on integer literal", line, col);
      t.kind = Tok::Num;
      t.text = s.substr(i, j - i);
      t.value = v;
      advance(j - i);
      out.push_back(t);
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t j = i;
      while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
      t.text = s.substr(i, j - i);
      t.kind = kKeywords.count(t.text) ? Tok::Keyword : Tok::Ident;
      advance(j - i);
      out.push_back(t);
      continue;
    }

    bool matched = false;
    for (const char* p : kPuncts) {
      const std::string punct(p);
      if (s.compare(i, punct.size(), punct) == 0) {
        t.kind = Tok::Punct;
        t.text = punct;
        advance(punct.size());
        out.push_back(t);
        matched = true;
        break;
      }
    }
    if (!matched) throw CompileError(std::string("unexpected character '") + c + "'", line, col);
  }

  Token eof;
  eof.kind = Tok::Eof;
  eof.text = "<eof>";
  eof.line = line;
  eof.col = col;
  out.push_back(eof);
  return out;
}
