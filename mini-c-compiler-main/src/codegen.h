// codegen.h - IR + register allocation -> x86-64 assembly (AT&T syntax,
// System V ABI, Linux). The output is assembled and linked with gcc.
#pragma once

#include <ostream>

#include "ir.h"
#include "regalloc.h"

struct CodegenOptions {
  bool fuseCompareBranch = true;  // emit cmp+jcc instead of setcc+test+jne
};

void emitFunction(const Function& fn, const Allocation& alloc, const CodegenOptions& opts,
                  std::ostream& os);
void emitPrologueFile(std::ostream& os);
void emitEpilogueFile(std::ostream& os);
