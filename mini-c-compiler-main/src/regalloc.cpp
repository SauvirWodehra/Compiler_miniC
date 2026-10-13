// regalloc.cpp - linear scan over live intervals.
//
// 1. Number instructions in block layout order.
// 2. Build one conservative live interval [start, end] per virtual register:
//    every definition/use position, widened to cover whole blocks where the
//    register is live-in / live-out (this is what makes loops correct: a value
//    live around a back edge spans the entire loop).
// 3. Sweep intervals by start point, keeping an "active" set sorted by end.
//    Expire intervals that ended, grab a free register, and when none is free
//    spill whichever interval ends furthest in the future.
#include "regalloc.h"

#include <algorithm>
#include <climits>

#include "liveness.h"

namespace {

struct Interval {
  int vreg;
  int start = INT_MAX;
  int end = -1;
  void cover(int p) {
    start = std::min(start, p);
    end = std::max(end, p);
  }
};

}  // namespace

Allocation allocateRegisters(const Function& fn, int numRegs) {
  Allocation result;
  result.loc.assign(fn.nregs, Location{});

  Liveness lv = computeLiveness(fn);
  std::vector<Interval> iv(fn.nregs);
  for (int r = 0; r < fn.nregs; ++r) iv[r].vreg = r;

  int pos = 0;
  std::vector<int> tmp;
  for (const auto& b : fn.blocks) {
    if (b.dead) continue;
    const int blockStart = pos;
    const int blockEnd = pos + static_cast<int>(b.ins.size()) * 2 - 1;
    for (int r = 0; r < fn.nregs; ++r) {
      if (lv.in[b.id][r]) iv[r].cover(blockStart);
      if (lv.out[b.id][r]) iv[r].cover(blockEnd);
    }
    for (const auto& in : b.ins) {
      tmp.clear();
      usesOf(in, tmp);
      for (int r : tmp) iv[r].cover(pos);
      if (in.dst >= 0) iv[in.dst].cover(pos + 1);  // defs land just after uses
      pos += 2;
    }
  }

  std::vector<Interval*> order;
  for (auto& i : iv)
    if (i.end >= 0) order.push_back(&i);
  std::sort(order.begin(), order.end(), [](const Interval* a, const Interval* b) {
    return a->start != b->start ? a->start < b->start : a->vreg < b->vreg;
  });

  std::vector<Interval*> active;  // sorted by increasing end
  std::vector<int> freeRegs;
  for (int r = numRegs - 1; r >= 0; --r) freeRegs.push_back(r);
  std::vector<bool> everUsed(numRegs, false);

  auto spill = [&](Interval* i) {
    result.loc[i->vreg] = {Location::Stack, result.numSlots++};
    ++result.spilledVregs;
  };
  auto insertActive = [&](Interval* i) {
    auto it = std::lower_bound(active.begin(), active.end(), i,
                               [](const Interval* a, const Interval* b) { return a->end < b->end; });
    active.insert(it, i);
  };

  for (Interval* cur : order) {
    // Expire intervals that end before this one starts; their registers free up.
    while (!active.empty() && active.front()->end < cur->start) {
      freeRegs.push_back(result.loc[active.front()->vreg].index);
      active.erase(active.begin());
    }

    if (!freeRegs.empty()) {
      int reg = freeRegs.back();
      freeRegs.pop_back();
      result.loc[cur->vreg] = {Location::Reg, reg};
      everUsed[reg] = true;
      ++result.allocatedVregs;
      insertActive(cur);
      continue;
    }

    if (numRegs == 0) {
      spill(cur);
      continue;
    }

    // No register free: spill the interval that lives longest.
    Interval* victim = active.back();
    if (victim->end > cur->end) {
      result.loc[cur->vreg] = result.loc[victim->vreg];  // steal its register
      active.pop_back();
      spill(victim);  // allocatedVregs unchanged: one in, one out
      insertActive(cur);
    } else {
      spill(cur);
    }
  }

  for (int r = 0; r < numRegs; ++r)
    if (everUsed[r]) result.usedRegs.push_back(r);
  return result;
}
