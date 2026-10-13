// liveness.cpp
#include "liveness.h"

Liveness computeLiveness(const Function& fn) {
  const size_t nb = fn.blocks.size();
  const size_t nr = static_cast<size_t>(fn.nregs);

  // use[b]: read in b before any write in b.  def[b]: written in b.
  std::vector<std::vector<bool>> use(nb, std::vector<bool>(nr)), def(nb, std::vector<bool>(nr));
  std::vector<int> tmp;
  for (const auto& b : fn.blocks) {
    if (b.dead) continue;
    for (const auto& in : b.ins) {
      tmp.clear();
      usesOf(in, tmp);
      for (int r : tmp)
        if (!def[b.id][r]) use[b.id][r] = true;
      if (in.dst >= 0) def[b.id][in.dst] = true;
    }
  }

  Liveness lv;
  lv.in.assign(nb, std::vector<bool>(nr));
  lv.out.assign(nb, std::vector<bool>(nr));

  // Visiting blocks in reverse layout order converges quickly for a backward problem.
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t k = nb; k-- > 0;) {
      const Block& b = fn.blocks[k];
      if (b.dead) continue;
      std::vector<bool> out(nr);
      for (int s : b.succ)
        for (size_t r = 0; r < nr; ++r)
          if (lv.in[s][r]) out[r] = true;
      std::vector<bool> in(nr);
      for (size_t r = 0; r < nr; ++r) in[r] = use[k][r] || (out[r] && !def[k][r]);
      if (in != lv.in[k] || out != lv.out[k]) {
        lv.in[k] = std::move(in);
        lv.out[k] = std::move(out);
        changed = true;
      }
    }
  }
  return lv;
}
