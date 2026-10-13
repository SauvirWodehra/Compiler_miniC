// main.cpp - compiler driver.
//
//   mcc [options] input.c
//     -o <file>        output assembly file (default: a.s, "-" for stdout)
//     -O0              no optimization; every value lives on the stack
//     -O1              optimizer + linear-scan register allocation (default)
//     --emit-tokens    print the token stream and stop
//     --emit-ast       print the syntax tree and stop
//     --emit-ir        print the (optimized, at -O1) IR and stop
//     --stats          print pass statistics to stderr
#include <fstream>
#include <iostream>
#include <sstream>

#include "codegen.h"
#include "irgen.h"
#include "lexer.h"
#include "opt.h"
#include "parser.h"
#include "regalloc.h"

namespace {

void usage() {
  std::cerr << "usage: mcc [-O0|-O1] [--emit-tokens|--emit-ast|--emit-ir] [--stats] "
               "[-o out.s] input.c\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string input, output = "a.s";
  int optLevel = 1;
  bool emitTokens = false, emitAst = false, emitIr = false, stats = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-O0") optLevel = 0;
    else if (a == "-O1") optLevel = 1;
    else if (a == "--emit-tokens") emitTokens = true;
    else if (a == "--emit-ast") emitAst = true;
    else if (a == "--emit-ir") emitIr = true;
    else if (a == "--stats") stats = true;
    else if (a == "-o" && i + 1 < argc) output = argv[++i];
    else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "mcc: unknown option '" << a << "'\n";
      usage();
      return 2;
    } else {
      input = a;
    }
  }
  if (input.empty()) {
    usage();
    return 2;
  }

  std::ifstream in(input);
  if (!in) {
    std::cerr << "mcc: cannot open '" << input << "'\n";
    return 1;
  }
  std::stringstream buf;
  buf << in.rdbuf();

  try {
    std::vector<Token> tokens = lex(buf.str());
    if (emitTokens) {
      for (const auto& t : tokens)
        std::cout << t.line << ":" << t.col << "\t" << tokKindName(t.kind) << "\t" << t.text << "\n";
      return 0;
    }

    Program prog = Parser(std::move(tokens)).parseProgram();
    if (emitAst) {
      dumpAST(prog, std::cout);
      return 0;
    }

    Module mod = generateIR(prog);

    int irBefore = 0, irAfter = 0;
    OptStats ost;
    for (auto& fn : mod.funcs) {
      irBefore += countInstrs(fn);
      if (optLevel >= 1) optimizeFunction(fn, ost);
      irAfter += countInstrs(fn);
    }

    if (emitIr) {
      printModule(mod, std::cout);
      return 0;
    }

    std::ostringstream asmOut;
    emitPrologueFile(asmOut);
    int spilled = 0, allocated = 0, slots = 0;
    CodegenOptions cg;
    cg.fuseCompareBranch = optLevel >= 1;
    for (const auto& fn : mod.funcs) {
      Allocation alloc = allocateRegisters(fn, optLevel >= 1 ? kNumAllocatable : 0);
      spilled += alloc.spilledVregs;
      allocated += alloc.allocatedVregs;
      slots += alloc.numSlots;
      emitFunction(fn, alloc, cg, asmOut);
    }
    emitEpilogueFile(asmOut);

    const std::string text = asmOut.str();
    if (output == "-") {
      std::cout << text;
    } else {
      std::ofstream out(output);
      if (!out) {
        std::cerr << "mcc: cannot write '" << output << "'\n";
        return 1;
      }
      out << text;
    }

    if (stats) {
      int asmLines = 0;
      for (size_t p = 0; (p = text.find("\n    ", p)) != std::string::npos; ++p)
        if (text.compare(p + 5, 1, ".") != 0) ++asmLines;
      std::cerr << "== mcc stats (-O" << optLevel << ") ==\n"
                << "  functions                       " << mod.funcs.size() << "\n"
                << "  IR instrs before opt            " << irBefore << "\n"
                << "  IR instrs after opt             " << irAfter << "\n";
      for (const auto& kv : ost.counts)
        std::cerr << "  pass " << kv.first << std::string(27 - std::min<size_t>(26, kv.first.size()), ' ')
                  << kv.second << "\n";
      std::cerr << "  vregs in registers              " << allocated << "\n"
                << "  vregs spilled                   " << spilled << "\n"
                << "  stack slots                     " << slots << "\n"
                << "  asm instructions                " << asmLines << "\n";
    }
  } catch (const CompileError& e) {
    std::cerr << input << ":" << e.line << ":" << e.col << ": error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
