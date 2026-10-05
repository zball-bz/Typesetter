// GENERATED from engine/data/math/stdlib.tsv by tools/mathdict.py — do not edit.
// The built-in math function rows (engine/src/math/ir.cc parses and checks
// them at start-up: checkRow).
#pragma once

namespace tsr { namespace mathrows {

struct StdRow {
  const char* signature;
  const char* body;
  const char* bare;  // "" = none
};
inline constexpr StdRow kStdlib[] = {
  {"sqrt(x)", "radical(#x)", ""},
  {"root(n, x)", "radical(#x, #n)", ""},
  {"abs(x)", "lr(|, #x, |)", ""},
  {"norm(x)", "lr(‖, #x, ‖)", ""},
  {"floor(x)", "lr(⌊, #x, ⌋)", ""},
  {"ceil(x)", "lr(⌈, #x, ⌉)", ""},
  {"binom(n, k)", "lr((, stack(#n, #k), ))", ""},
  {"overline(x)", "rule(#x, over)", ""},
  {"underline(x)", "rule(#x, under)", ""},
  {"bar(x)", "rule(#x, over)", ""},
  {"hat(x)", "accent(#x, ˆ)", "ˆ"},
  {"tilde(x)", "accent(#x, ˜)", "˜"},
  {"vec(x)", "accent(#x, ⃗)", ""},
  {"dot(x)", "accent(#x, ˙)", "cdot"},
  {"ddot(x)", "accent(#x, ¨)", "¨"},
  {"breve(x)", "accent(#x, ˘)", "˘"},
  {"check(x)", "accent(#x, ˇ)", "ˇ"},
  {"ring(x)", "accent(#x, ˚)", "˚"},
  {"acute(x)", "accent(#x, ´)", "´"},
  {"grave(x)", "accent(#x, `)", "`"},
};

}}  // namespace tsr::mathrows
