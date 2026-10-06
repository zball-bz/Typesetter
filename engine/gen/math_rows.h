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
  {"thin()", "space(3)", ""},
  {"med()", "space(4)", ""},
  {"thick()", "space(5)", ""},
  {"quad()", "space(18)", ""},
  {"wide()", "space(36)", ""},
  {"display(x)", "mstyle(#x, display)", ""},
  {"inline(x)", "mstyle(#x, text)", ""},
  {"script(x)", "mstyle(#x, script)", ""},
  {"sscript(x)", "mstyle(#x, sscript)", ""},
  {"limits(x)", "mlimits(#x, limits)", ""},
  {"scripts(x)", "mlimits(#x, scripts)", ""},
  {"bb(x)", "variant(#x, bb)", ""},
  {"cal(x)", "variant(#x, cal)", ""},
  {"frak(x)", "variant(#x, frak)", ""},
  {"bold(x)", "variant(#x, bold)", ""},
  {"italic(x)", "variant(#x, italic)", ""},
  {"sans(x)", "variant(#x, sans)", ""},
  {"mono(x)", "variant(#x, mono)", ""},
};

}}  // namespace tsr::mathrows
