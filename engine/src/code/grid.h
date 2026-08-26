// Rational grid solving (verbatim-design §2/§3): Stern–Brocot walk to the
// first convergent p/q meeting BOTH the error budget (per-row drift under
// half a pixel) and q ≤ 7 (beyond that a grid stops reading as monospace).
// Snap-kerning deltas pull true advances onto the grid: the atom is chosen
// from the WIDER side so the thinner script gains positive letter-spacing
// (negative spacing squeezes ink and is never emitted).
#pragma once
#include "../support/support.h"

namespace tsr {

struct GridSpec {
  int p = 2, q = 1;        // cjk : latin atoms
  bool exact = false;      // met the error budget within q ≤ 7
  double atomPx = 0;       // grid atom g
  double dLatinPx = 0;     // snap letter-spacing for Latin runs (≥ 0)
  double dCjkPx = 0;       // snap letter-spacing for CJK runs (≥ 0)
};

inline GridSpec solveGrid(double chLatinPx, double chCjkPx, int maxCols) {
  GridSpec g;
  if (chLatinPx <= 0 || chCjkPx <= 0) return g;
  double r = chCjkPx / chLatinPx;
  // walk the Stern–Brocot path (it visits every best rational
  // approximation) and keep the q ≤ 7 mediant with the smallest error —
  // snap-kerning forces rendering onto the grid, so the ratio need not be
  // exact; `exact` reports whether NATURAL flow would also stay within the
  // half-pixel-per-row drift budget.
  int pl = 0, ql = 1, pr = 1, qr = 0;
  int p = 2, q = 1;
  double bestErr = 1e18;
  for (int it = 0; it < 64; it++) {
    int pm = pl + pr, qm = ql + qr;
    if (qm > 7) break;
    double approx = (double)pm / (double)qm;
    double err = r > approx ? r - approx : approx - r;
    if (err < bestErr) {
      bestErr = err;
      p = pm;
      q = qm;
    }
    if (err < 1e-12) break;
    if (approx < r) { pl = pm; ql = qm; } else { pr = pm; qr = qm; }
  }
  g.p = p;
  g.q = q;
  g.exact =
      bestErr * chLatinPx * (double)(maxCols > 0 ? maxCols : 80) < 0.5;
  // atom from the wider side: the thinner script gets positive spacing
  double gL = chLatinPx / (double)q;   // latin-exact atom
  double gC = chCjkPx / (double)p;     // cjk-exact atom
  g.atomPx = gL > gC ? gL : gC;
  g.dLatinPx = g.atomPx * q - chLatinPx;
  g.dCjkPx = g.atomPx * p - chCjkPx;
  if (g.dLatinPx < 1e-9) g.dLatinPx = 0;
  if (g.dCjkPx < 1e-9) g.dCjkPx = 0;
  return g;
}

}  // namespace tsr
