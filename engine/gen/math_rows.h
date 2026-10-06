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
  {"mat(r: cells)", "inline(lr((, grid(c, #r), )))", ""},
  {"pmat(r: cells)", "inline(lr((, grid(c, #r), )))", ""},
  {"bmat(r: cells)", "inline(lr([, grid(c, #r), ]))", ""},
  {"Bmat(r: cells)", "inline(lr({, grid(c, #r), }))", ""},
  {"vmat(r: cells)", "inline(lr(|, grid(c, #r), |))", ""},
  {"Vmat(r: cells)", "inline(lr(‖, grid(c, #r), ‖))", ""},
  {"cases(r: rows)", "inline(lr({, grid(l, #r), .))", ""},
  {"aligned(r: rows)", "grid(rl, #r)", ""},
  {"overbrace(x, t?)", "attach(hstretch(#x, ⏞, over), t: #t)", ""},
  {"underbrace(x, b?)", "attach(hstretch(#x, ⏟, under), b: #b)", ""},
  {"overbracket(x, t?)", "attach(hstretch(#x, ⎴, over), t: #t)", ""},
  {"underbracket(x, b?)", "attach(hstretch(#x, ⎵, under), b: #b)", ""},
  {"overparen(x, t?)", "attach(hstretch(#x, ⏜, over), t: #t)", ""},
  {"underparen(x, b?)", "attach(hstretch(#x, ⏝, under), b: #b)", ""},
  {"big(d: sym)", "delim(#d, 1.2)", ""},
  {"Big(d: sym)", "delim(#d, 1.8)", ""},
  {"bigg(d: sym)", "delim(#d, 2.4)", ""},
  {"Bigg(d: sym)", "delim(#d, 3)", ""},
  {"hphantom(x)", "phantom(#x, h)", ""},
  {"vphantom(x)", "phantom(#x, v)", ""},
  {"smash(x)", "phantom(#x, smash)", ""},
};

}}  // namespace tsr::mathrows
