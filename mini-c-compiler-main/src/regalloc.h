// regalloc.h - linear-scan register allocation (Poletto & Sarkar, 1999).
#pragma once

#include <vector>

#include "ir.h"

struct Location {
  enum Kind { None, Reg, Stack } kind = None;
  int index = 0;  // physical register index, or stack slot number
};

struct Allocation {
  std::vector<Location> loc;   // per virtual register
  int numSlots = 0;            // stack slots needed for spills
  std::vector<int> usedRegs;   // physical registers used (must be saved/restored)
  int spilledVregs = 0;
  int allocatedVregs = 0;
};

// Number of allocatable physical registers. We only hand out callee-saved
// registers (rbx, r12-r15) so values survive calls without caller-save code.
constexpr int kNumAllocatable = 5;

// numRegs = 0 puts every virtual register on the stack (the -O0 strategy).
Allocation allocateRegisters(const Function& fn, int numRegs);
