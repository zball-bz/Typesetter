// The paragraph context (plan P4-02; design T5 Shaper, findings
// emitter/paragraph-blind-script-context, emitter/latin-quote-heuristic):
// an inline stream's clusters in reading order across node edges — the text
// of every node, the evidence code and objects give, the neutral blanks —
// and what its ambiguous marks resolve to. Markup never resets it: a style,
// a link, a reference or inline code between two characters changes their
// runs, not their typography. Emit's shaper and the soft-break pass of the
// model (model/softbreak.h) read the same resolution, so the typeset page
// and the text of the tree agree.
#pragma once
#include <string_view>
#include <vector>

#include "textrules.h"

namespace tsr {

// (plan P3-30; finding markup-language/quote-context-heuristic) how the
// ambiguous marks — curly quotes, the em dash, the ellipsis — of a run are
// set: as its own language says (zh, ja, ko: CJK punctuation; another:
// Latin glyphs), or, a run without one, by its context
enum class MarkClass : u8 { Neighbours, Cjk, Latin };
inline MarkClass markClassOf(std::string_view tag) {
  if (tag.empty()) return MarkClass::Neighbours;
  tag = tag.substr(0, tag.find_first_of("-_"));
  auto is = [&](const char* l) { return tag.size() == 2 && (tag[0] | 32) == l[0] && (tag[1] | 32) == l[1]; };
  return is("zh") || is("ja") || is("ko") ? MarkClass::Cjk : MarkClass::Latin;
}

struct CtxEntry {
  enum K : u8 {
    Char,    // a cluster of text: cp is its first codepoint
    Blank,   // a space, a tab, a soft break: no evidence either way
    Narrow,  // inline code, a formula, an error box: Latin-class evidence
    Opaque,  // an image, raw markup, a hard break, a fill: none
  } k = Char;
  MarkClass marks = MarkClass::Neighbours;  // Char: its run's
  bool wide = false;  // Char, resolved: set as CJK (an ideograph, wide punctuation, a wide ambiguous mark)
  bool apostrophe = false;  // Char: U+2019 between letters (UAX #29 MidLetter), never a quote
  u32 cp = 0;
};

// a cluster's entry, its width class preset (CJK: an ideograph or wide
// punctuation); `ambiguous` notes a mark resolveContext has to settle
inline CtxEntry ctxChar(u32 cp, MarkClass marks, bool& ambiguous) {
  CtxEntry e;
  e.cp = cp;
  e.marks = marks;
  if (cp >= kCCRanges[1].start) {  // (below the table's first break: one class, the common one)
    const CC c = ccOf(cp);
    e.wide = kCCFlags[(u8)c] & kCC_wide;
    ambiguous = ambiguous || c == CC::AmbOpenQuote || c == CC::AmbCloseQuote || c == CC::AmbDash ||
                c == CC::AmbEllipsis;
  }
  return e;
}

namespace context_detail {
inline bool wideAt(const std::vector<CtxEntry>& v, size_t i) { return v[i].k == CtxEntry::Char && v[i].wide; }
// a quote's evidence ahead (the rule real-world-report #3 set down): a CJK
// character, or punctuation, right after it
inline bool wideAhead(const std::vector<CtxEntry>& v, size_t i) {
  if (i + 1 >= v.size() || v[i + 1].k != CtxEntry::Char) return false;
  const u32 c = v[i + 1].cp;
  return isWide(c) || isOpenPunct(c) || isClosePunct(c);
}
inline bool wordAt(const std::vector<CtxEntry>& v, size_t i) {
  return i < v.size() && v[i].k == CtxEntry::Char && isWordChar(v[i].cp);
}
}  // namespace context_detail

// Resolves the ambiguous marks of a stream, in place:
// - the em dash and the ellipsis are CJK (a defined-width box) as their
//   run's language says, else when doubled or beside a CJK character or
//   wide punctuation — an English dash between words stays Latin;
// - U+2019 between letters is an apostrophe: Latin, part of its word;
// - a curly quote is CJK punctuation as its run's language says, else when
//   the character before it is CJK or wide punctuation (a 、 or ：
//   before “ is CJK evidence: finding emitter/missed:4), or when a CJK
//   character or punctuation follows it; a matched pair (“ ”, ‘ ’) resolves
//   jointly — either quote's evidence sets both — so a pair is never split
//   between two fonts. A blank between gives no evidence: a quote set off
//   by spaces stays Latin.
// The entries come from ctxChar (their width preset); a stream without an
// ambiguous mark has nothing to settle.
inline void resolveContext(std::vector<CtxEntry>& v, bool ambiguous) {
  using namespace context_detail;
  if (!ambiguous) return;
  const size_t n = v.size();
  // dashes and ellipses, in reading order (a resolved one is evidence)
  for (size_t i = 0; i < n; i++) {
    CtxEntry& e = v[i];
    if (e.k != CtxEntry::Char || !isAmbDashOrEllipsis(e.cp)) continue;
    if (e.marks != MarkClass::Neighbours) {
      e.wide = e.marks == MarkClass::Cjk;
      continue;
    }
    const bool pair = (i + 1 < n && v[i + 1].k == CtxEntry::Char && v[i + 1].cp == e.cp) ||
                      (i > 0 && v[i - 1].k == CtxEntry::Char && v[i - 1].cp == e.cp);
    const bool after = i + 1 < n && v[i + 1].k == CtxEntry::Char &&
                       (isWide(v[i + 1].cp) || isAmbDashOrEllipsis(v[i + 1].cp));
    e.wide = pair || (i > 0 && wideAt(v, i - 1)) || after;
  }
  // quotes: each one's own evidence, then the pairs
  std::vector<u8> own(n, 0);  // 1 narrow, 2 wide (explicit or by evidence)
  for (size_t i = 0; i < n; i++) {
    CtxEntry& e = v[i];
    if (e.k != CtxEntry::Char || !isAmbQuote(e.cp)) continue;
    if (e.cp == 0x2019 && i > 0 && wordAt(v, i - 1) && wordAt(v, i + 1)) {
      e.apostrophe = true;
      e.wide = false;
      continue;
    }
    if (e.marks != MarkClass::Neighbours) e.wide = e.marks == MarkClass::Cjk;
    else e.wide = (i > 0 && wideAt(v, i - 1)) || wideAhead(v, i);
    own[i] = e.wide ? 2 : 1;
  }
  std::vector<size_t> open;  // unmatched openers, innermost last
  for (size_t i = 0; i < n; i++) {
    if (!own[i]) continue;
    const u32 cp = v[i].cp;
    if (cp == 0x201C || cp == 0x2018) {
      open.push_back(i);
      continue;
    }
    const u32 want = cp == 0x201D ? 0x201C : 0x2018;
    size_t m = open.size();
    while (m > 0 && v[open[m - 1]].cp != want) m--;
    if (m == 0) continue;  // unmatched: its own evidence
    const size_t o = open[m - 1];
    open.resize(m - 1);
    const bool wide = own[o] == 2 || own[i] == 2;
    if (v[o].marks == MarkClass::Neighbours) v[o].wide = wide;
    if (v[i].marks == MarkClass::Neighbours) v[i].wide = wide;
  }
}

}  // namespace tsr
