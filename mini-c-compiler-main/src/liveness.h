// liveness.h - backward dataflow analysis: which virtual registers hold a
// value that may still be read later. Used by dead-code elimination and by
// the register allocator to build live intervals.
#pragma once

#include <vector>

#include "ir.h"

struct Liveness {
  // Dense bitsets indexed [block][vreg].
  std::vector<std::vector<bool>> in, out;
};

// Solves  out[b] = U in[s] over successors s
//         in[b]  = use[b] U (out[b] - def[b])
// iterating to a fixed point.
Liveness computeLiveness(const Function& fn);
