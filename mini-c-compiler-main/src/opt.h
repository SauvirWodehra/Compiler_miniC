// opt.h - machine-independent optimization passes on the IR.
#pragma once

#include <map>
#include <string>

#include "ir.h"

struct OptStats {
  std::map<std::string, int> counts;  // pass name -> number of rewrites
  void add(const std::string& k, int n = 1) { counts[k] += n; }
};

// Runs all passes to a fixed point:
//   global constant propagation, local value numbering (constant/copy
//   propagation, constant folding, algebraic simplification, strength
//   reduction, common-subexpression elimination), liveness-based dead-code
//   elimination, and CFG simplification (branch folding, unreachable-block
//   removal, jump threading, block merging).
void optimizeFunction(Function& fn, OptStats& stats);
