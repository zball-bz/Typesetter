#include "linepass.h"
#include "../syntax/labels.h"

#include <algorithm>
#include <optional>

#include "../inline/jslex.h"
#include "../syntax/cursor.h"
#include "../syntax/lexer.h"

namespace tsr {

namespace {

constexpr u32 kTabStop = 4;  // App B: a tab advances to the next multiple of 4
constexpr u32 kNone = ~0u;

// The container protocol (design T1 BlockAutomaton): a Prefix container
// continues on lines with its prefix ('>'), a Column container on blank lines
// and lines indented to its content column, an Explicit container (a region)
// until its named closer.
enum class Shape : u8 { Prefix, Column, Explicit };

struct OpenC {
  SkelNode* node;            // Quote / Item / Region
  Shape shape;
  u32 contentCol = 0;        // Column: required continuation column
  SkelNode* list = nullptr;  // Column: the item's list (its span follows)
};

bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool isLet(std::string_view r) {
  return starts(r, "#let") && (r.size() == 4 || r[4] == ' ' || r[4] == '\t');
}

struct LinePass {
  const SourceText& src;
  const std::vector<Span>& lines;  // the physical lines (raw, terminators excluded)
  Arena& arena;
  DiagSink& diags;
  std::string_view all;
  u32 depth = 0;  // the levels around these lines (a content body's)
  u32 nlines = 0;

  SkelNode* root = nullptr;
  std::vector<OpenC> open;   // container stack, outermost first
  SkelNode* leaf = nullptr;  // open paragraph (always in the innermost container)
  std::vector<RevertedWindow> windows;

  // A verbatim carry — a fence or a block comment — continues over the
  // following lines while the containers it opened in continue; container
  // exit ends it.
  enum class Carry : u8 { None, Fence, Comment };
  Carry carry = Carry::None;
  SkelNode* carryNode = nullptr;
  size_t carryDepth = 0;
  u32 fenceTicks = 0, fenceIndent = 0;
  int commentDepth = 0;

  SkelNode* mk(SkelKind k) {
    SkelNode* n = arena.make<SkelNode>();
    n->kind = k;
    return n;
  }
  SkelNode* parent() { return open.empty() ? root : open.back().node; }
  void closeLeaf() {
    leaf = nullptr;
    owning = false;
  }

  // Line ownership (plan P1-08; design T1 BlockAutomaton steps 2, 4, 5):
  // when a paragraph line leaves a construct open, the pass looks ahead once
  // to the construct's structural bound — a blank line or container exit for
  // Leaf-owned constructs (code spans, math, splice JS, inline-form content
  // bodies), container exit for Container-owned ones (comments, block-form
  // bodies). If it closes there, the lines up to its closer belong to the
  // paragraph and no block starts on them; otherwise its opener is literal
  // text (a RevertedWindow records the lines it could not own) and the scan
  // goes on after it. No line is processed twice.
  bool owning = false;
  bool ownBlock = false;    // the owned lines are a block-form body
  u32 ownUntil = 0;         // the last owned line
  u32 ownResume = 0;        // raw offset on it where the scan resumes
  bool resumeAtBody = false;  // ... at a splice's block-form '['
  bool chainNext = false;     // the owned body is an #if's: an else link may follow its ']'
  Span blockBody;           // the block-form body: its '[' and ']'
  std::vector<u32> leafLines, leafCols;  // per paragraph line: line, indent

  u32 lineStart(u32 l) const { return lines[l].start; }
  u32 lineEnd(u32 l) const { return lines[l].end; }

  static u32 advance(char c, u32 col) { return c == '\t' ? (col / kTabStop + 1) * kTabStop : col + 1; }
  // skip spaces and tabs while the column is below `limit`
  void skipBlanks(u32 le, u32& pos, u32& col, u32 limit = kNone) const {
    while (pos < le && (all[pos] == ' ' || all[pos] == '\t') && col < limit) {
      col = advance(all[pos], col);
      pos++;
    }
  }
  bool isBlank(u32 a, u32 b) const {
    for (u32 p = a; p < b; p++)
      if (all[p] != ' ' && all[p] != '\t' && all[p] != '\r') return false;
    return true;
  }
  static void grow(SkelNode* n, u32 end) {
    if (end > n->span.end) n->span.end = end;
  }
  // a line attributed to every open container extends their spans
  void extend(u32 le) {
    for (OpenC& c : open) {
      grow(c.node, le);
      if (c.list) grow(c.list, le);
    }
  }

  void regionUnclosed(const SkelNode* rg) {
    diags.add(Sev::Error, "region-unclosed", rg->span,
              "region '#!" + std::string(src.slice(rg->langSpan)) + "' has no matching closer");
  }
  // pop to `depth`; a region closed this way never saw its closer
  void closeTo(size_t depth) {
    closeLeaf();
    while (open.size() > depth) {
      if (open.back().shape == Shape::Explicit) regionUnclosed(open.back().node);
      open.pop_back();
    }
  }

  // --- per-line container matching (pure) ----------------------------------
  // pos/col walk past the prefixes of the matched containers; returns how many
  // of the open containers (outermost first) the line continues.
  size_t matchContainers(u32 le, u32& pos, u32& col, bool blank) const {
    size_t matched = 0;
    for (const OpenC& c : open) {
      if (c.shape == Shape::Explicit) {
        matched++;
        continue;
      }
      if (c.shape == Shape::Prefix) {
        u32 p = pos, cl = col;
        skipBlanks(le, p, cl);
        if (p < le && all[p] == '>') {
          p++;
          cl++;
          if (p < le && (all[p] == ' ' || all[p] == '\t')) cl = advance(all[p++], cl);
          pos = p;
          col = cl;
          matched++;
          continue;
        }
        break;
      }
      // Column: blank lines stay inside; content must reach contentCol
      if (blank) {
        matched++;
        continue;
      }
      u32 p = pos, cl = col;
      skipBlanks(le, p, cl, c.contentCol);
      if (cl < c.contentCol) break;
      pos = p;
      col = cl;
      matched++;
    }
    return matched;
  }

  // --- container starters ----------------------------------------------------
  // Quotes always interrupt a paragraph; a list item only when it is not
  // empty, and an 'N.' item only when N is 1 ('1984. Then' continues it).
  void tryStarters(u32 le, u32& pos, u32& col) {
    for (;;) {
      u32 p = pos, cl = col;
      skipBlanks(le, p, cl);
      if (p >= le) return;
      char c = all[p];
      if (c == '>') {
        if (nestFull()) {
          nestLimit({p, p + 1});
          return;
        }
        closeLeaf();
        SkelNode* q = mk(SkelKind::Quote);
        q->span = {p, le};
        parent()->kids.push_back(q);
        open.push_back({q, Shape::Prefix});
        p++;
        cl++;
        if (p < le && (all[p] == ' ' || all[p] == '\t')) cl = advance(all[p++], cl);
        pos = p;
        col = cl;
        continue;
      }
      char marker = 0;
      u32 markerLen = 0;
      int num = 1;
      u32 colon = kNone;  // a description item's (plan P3-34)
      auto blankAt = [&](u32 q) { return q < le && (all[q] == ' ' || all[q] == '\t'); };
      if ((c == '-' || c == '+') && blankAt(p + 1)) {
        marker = c;
        markerLen = 1;
      } else if (c == '/' && blankAt(p + 1)) {
        // (plan P3-34; D-L08) `/ term: description`: no term colon, no item
        colon = termColon(p + 2, le);
        if (colon == kNone || isBlank(p + 2, colon)) return;
        marker = '/';
        markerLen = 1;
      } else if (c >= '0' && c <= '9') {
        u32 q = p;
        int n = 0;
        while (q < le && all[q] >= '0' && all[q] <= '9' && q - p < 9) n = n * 10 + (all[q++] - '0');
        if (q < le && all[q] == '.' && blankAt(q + 1)) {
          marker = '.';
          markerLen = (q + 1) - p;
          num = n;
        }
      }
      if (!marker) return;
      if (leaf && (isBlank(p + markerLen, le) || (marker == '.' && num != 1))) return;
      if (nestFull()) {
        nestLimit({p, p + markerLen});
        return;
      }
      closeLeaf();
      const u32 markerCol = cl;
      u32 after = p + markerLen, acol = cl + markerLen;
      acol = advance(all[after], acol);  // the blank after the marker
      after++;
      // list identity: (marker class, column)
      SkelNode* par = parent();
      SkelNode* list = nullptr;
      if (!par->kids.empty() && par->kids.back()->kind == SkelKind::List &&
          par->kids.back()->marker == marker && par->kids.back()->markerCol == markerCol)
        list = par->kids.back();
      if (!list) {
        list = mk(SkelKind::List);
        list->ordered = marker == '+' || marker == '.';
        list->marker = marker;
        list->markerCol = markerCol;
        list->start = num;
        list->span = {p, le};
        par->kids.push_back(list);
      } else if (marker == '.') {
        int expect = list->start + (int)list->kids.size();
        if (num != expect && num != list->start)
          diags.add(Sev::Info, "list-number", {p, p + markerLen},
                    "item " + std::to_string(num) + " continues a list numbered from " +
                        std::to_string(list->start) + "; it is shown as " + std::to_string(expect));
      }
      SkelNode* item = mk(SkelKind::Item);
      item->span = {p, le};
      list->kids.push_back(item);
      grow(list, le);
      open.push_back({item, Shape::Column, acol, list});
      if (colon != kNone) {  // its term; its content starts after the colon (and a blank)
        u32 ts = after, te = colon;
        while (ts < te && (all[ts] == ' ' || all[ts] == '\t')) ts++;
        while (te > ts && (all[te - 1] == ' ' || all[te - 1] == '\t')) te--;
        item->termSpan = {ts, te};
        u32 q = colon + 1, qc = acol;
        for (u32 k = after; k < q; k++) qc = advance(all[k], qc);
        if (blankAt(q)) qc = advance(all[q++], qc);
        after = q;
        acol = qc;
      }
      pos = after;
      col = acol;
    }
  }

  // (plan P3-34) a description item's term colon: the first `:` after `q`
  // with a blank or the line's end after it, outside atoms (a code span, a
  // formula, a URL) and escapes; kNone when there is none
  u32 termColon(u32 q, u32 le) const {
    const std::string_view t = all.substr(0, le);
    while (q < le) {
      if (all[q] == '\\') {
        q += 2;
        continue;
      }
      if (const u32 e = atomEnd(t, q); e > q) {
        q = e;
        continue;
      }
      if (all[q] == ':' && (q + 1 >= le || all[q + 1] == ' ' || all[q + 1] == '\t')) return q;
      q++;
    }
    return kNone;
  }

  // A block-granular parse error: diagnostic + an Error leaf that lowers to
  // an error node in place (plan P0-05).
  void errorBlock(Span sp, const char* code, std::string msg) {
    closeLeaf();
    diags.add(Sev::Error, code, sp, msg);
    SkelNode* e = mk(SkelKind::Error);
    e->span = sp;
    e->errCode = code;
    e->errMsg = std::move(msg);
    parent()->kids.push_back(e);
  }

  // a container opened here would nest too deeply (kMaxNesting): its
  // marker stays text, said once per pass
  bool nestFull() const { return depth + open.size() >= kMaxNesting; }
  bool nestReported = false;
  void nestLimit(Span sp) {
    if (nestReported) return;
    nestReported = true;
    diags.add(Sev::Error, "nest-limit", sp,
              "containers nest more than " + std::to_string(kMaxNesting) + " deep: the rest is text");
  }

  // '#name!' alone on its line: the region name, else empty
  std::string_view closerName(u32 pos, u32 le) const {
    if (pos + 2 >= le || all[pos] != '#' || !isIdentStart(all[pos + 1])) return {};
    u32 np = pos + 1;
    while (np < le && isIdentCont(all[np])) np++;
    if (np >= le || all[np] != '!' || !isBlank(np + 1, le)) return {};
    return all.substr(pos + 1, np - (pos + 1));
  }
  // index + 1 of the innermost open region named `name`, 0 when none
  size_t openRegion(std::string_view name) const {
    for (size_t ri = open.size(); ri > 0; ri--)
      if (open[ri - 1].shape == Shape::Explicit && src.slice(open[ri - 1].node->langSpan) == name)
        return ri;
    return 0;
  }

  // Does a line (content from p) start a block? Recovery heuristic for a
  // broken statement (plan P0-04).
  bool startsBlock(u32 p, u32 le) const {
    std::string_view r = all.substr(p, le - p);
    bool block = starts(r, "```") || starts(r, "#!") || starts(r, "#let") || starts(r, "#{") ||
                 starts(r, "> ") || r == ">" || starts(r, "- ") || starts(r, "+ ") ||
                 starts(r, "%--") || starts(r, "---");
    if (!block && !r.empty() && r[0] == '=') {
      size_t n = 0;
      while (n < r.size() && r[n] == '=') n++;
      block = n <= 6 && n < r.size() && r[n] == ' ';
    }
    if (!block && !r.empty() && r[0] >= '0' && r[0] <= '9') {
      size_t n = 0;
      while (n < r.size() && r[n] >= '0' && r[n] <= '9') n++;
      block = n + 1 < r.size() && r[n] == '.' && r[n + 1] == ' ';
    }
    if (!block && r.size() >= 3 && r[0] == '#' && r.back() == '!') block = true;  // #name!
    return block;
  }

  // The last line of a broken statement opened on line `ln`: the statement
  // extends over the following lines of its containers up to a blank line, a
  // line that starts a block, or container exit.
  u32 recoverStatement(u32 ln) const {
    u32 last = ln;
    for (u32 l = ln + 1; l < nlines; l++) {
      u32 ls = lineStart(l), le = lineEnd(l), p = ls, c = 0;
      bool blank = isBlank(ls, le);
      if (blank || matchContainers(le, p, c, blank) < open.size()) break;
      skipBlanks(le, p, c);
      if (isBlank(p, le) || startsBlock(p, le)) break;
      last = l;
    }
    return last;
  }

  // --- leaves ----------------------------------------------------------------
  u32 trimEnd(u32 pos, u32 le) const {
    while (le > pos && (all[le - 1] == ' ' || all[le - 1] == '\t' || all[le - 1] == '\r')) le--;
    return le;
  }
  void addParaLine(u32 ln, u32 pos, u32 col, u32 le) {
    const u32 e = trimEnd(pos, le);
    if (e <= pos) return;
    if (!leaf) {
      leaf = mk(SkelKind::Para);
      leaf->span = {pos, e};
      parent()->kids.push_back(leaf);
      leafLines.clear();
      leafCols.clear();
    }
    pushLeafLine(ln, {pos, e}, col);
    scanLine(ln, pos, e);
  }
  void pushLeafLine(u32 ln, Span sp, u32 col) {
    leaf->lineSpans.push_back(sp);
    if (sp.end > leaf->span.end) leaf->span.end = sp.end;
    leafLines.push_back(ln);
    leafCols.push_back(col);
  }

  // --- line ownership --------------------------------------------------------
  bool reverted(u32 raw) const {
    return std::binary_search(leaf->literalAt.begin(), leaf->literalAt.end(), raw);
  }
  void makeLiteral(u32 raw, u32 line, u32 bound) {  // literalAt stays sorted
    std::vector<u32>& la = leaf->literalAt;
    la.insert(std::lower_bound(la.begin(), la.end(), raw), raw);
    windows.push_back({line, bound});
  }
  // only blanks, or a comment closed on the line, up to the end of the line
  static bool endsLine(std::string_view t, u32 p) {
    auto blanks = [&] {
      while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\r')) p++;
    };
    blanks();
    if (p + 2 < t.size() && t[p] == '%' && t[p + 1] == '-' && t[p + 2] == '-') {
      u32 end;
      if (!lexComment(t, p, end) || t.substr(p, end - p).find('\n') != std::string_view::npos) return false;
      p = end;
      blanks();
    }
    return p >= t.size() || t[p] == '\n';
  }

  // The construct starting at t[i] read with the inline lexer's primitives:
  // one past its end, or kNone. When it is left open, `opener` is the
  // unclosed opener (a code run, '$', '%--', a call's '(', a body's '['),
  // `container` marks a comment and `block` a block-form body (a '[' that
  // ends its line) — whose closer is a line, not a bracket.
  struct Left {
    u32 opener = kNone;
    bool container = false, block = false;
    bool chain = false;  // a block-form body of an else-chain keyword (#if, plan P2-12)
  };
  // (`args`: t[i] continues a splice's argument list, after a body's ']';
  // `elseLink`: t[i] may continue an #if after a body's ']', plan P2-12)
  // (`bm`: a matcher over `t` shared by the calls of one scan, so a line
  // of unclosed bodies costs one bracket scan, not one per opener)
  template <class RawOf>
  u32 constructEnd(std::string_view t, u32 i, RawOf rawOf, Left& left, bool args = false,
                   bool elseLink = false, BracketMatcher* bm = nullptr) const {
    const u32 n = (u32)t.size();
    std::optional<BracketMatcher> own;
    auto bodyEnd = [&](u32 p) -> u32 {  // a content body's '[' at p
      if (reverted(rawOf(p))) return kNone;
      if (endsLine(t, p + 1)) {
        left = {p, true, true};
        return kNone;
      }
      if (!bm) bm = &own.emplace(t);
      i32 close = bm->body(p);
      if (close < 0) {
        left = {p, false, false};
        return kNone;
      }
      return (u32)close + 1;
    };
    auto argList = [&](u32 p) -> u32 {
      while (p < n && t[p] == '[') {
        u32 e = bodyEnd(p);
        if (e == kNone) return left.opener == kNone ? p : kNone;  // a reverted '[' ends the args
        p = e;
      }
      return p;
    };
    // a keyword form's bodies from the '[' at p, with its else links
    auto keywordBodies = [&](u32 p, bool chain) -> u32 {
      for (;;) {
        const u32 e = bodyEnd(p);
        if (e == kNone) {
          left.chain = left.block && chain;
          return kNone;
        }
        KwElse ke;
        if (!chain || !lexElse(t, e, ke)) return e;
        p = ke.bodyOpen;
      }
    };
    if (elseLink) {
      KwElse ke;
      if (!lexElse(t, i, ke)) return kNone;
      return keywordBodies(ke.bodyOpen, true);
    }
    if (args) return argList(i);
    switch (inlineOpener(t, i)) {
      case InlineRule::code: {
        CodeSpanLex cs;
        if (lexCodeSpan(t, i, cs)) return cs.end;
        left = {i, false, false};
        return kNone;
      }
      case InlineRule::math: {
        u32 close;
        if (lexMath(t, i, close)) return close + 1;
        if (i + 1 >= n || t[i + 1] != '$') left = {i, false, false};  // "$$" is literal
        return kNone;
      }
      case InlineRule::comment: {
        u32 end;
        if (lexComment(t, i, end)) return end;
        left = {i, true, false};
        return kNone;
      }
      case InlineRule::splice: {
        KwHead kh;  // a keyword form (plan P2-12): its bodies and else links
        if (lexKeywordHead(t, i, kh)) return keywordBodies(kh.bodyOpen, kKeywords[kh.kw].elseChain);
        if (kh.kw >= 0 && kh.openAt != kNoPos && !reverted(rawOf(kh.openAt))) {
          left = {kh.openAt, false, false};
          return kNone;
        }
        SpliceLex sl;
        bool ok = lexSplice(t, i, sl);
        if (sl.openAt != kNoPos && !reverted(rawOf(sl.openAt))) {
          left = {sl.openAt, false, false};
          return kNone;
        }
        if (!ok) return kNone;
        return argList(sl.end);
      }
      case InlineRule::note: {
        u32 e = bodyEnd(i + 1);
        return e == kNone && left.opener == kNone ? i + 1 : e;
      }
      case InlineRule::url: {  // (plan P3-33) verbatim to its end: no opener inside it is one
        u32 s, e;
        return lexUrl(t, i, s, e) ? e : kNone;
      }
      case InlineRule::strong:
      case InlineRule::em:
      case InlineRule::link:
      case InlineRule::ref:
      case InlineRule::refs:
      case InlineRule::brk:
      case InlineRule::none:
        break;
    }
    return kNone;
  }

  // Scan a paragraph line's text [from, e) for constructs it leaves open
  // (`args`: `from` continues a splice's argument list).
  void scanLine(u32 ln, u32 from, u32 e, bool args = false) {
    const std::string_view t = all.substr(0, e);  // offsets are raw
    auto rawOf = [](u32 v) { return v; };
    BracketMatcher bm(t);
    u32 i = from;
    if (resumeAtBody) {  // a splice's block-form body opens here
      resumeAtBody = false;
      if (ownBodyLines(ln, i)) return;
      i++;
    }
    if (args && chainNext) {  // an #if's body closed: `else [ … ]` may follow (plan P2-12)
      chainNext = false;
      Left left;
      u32 end = constructEnd(t, i, rawOf, left, false, true, &bm);
      if (end != kNone) {
        i = end;
        args = false;
      } else if (left.opener != kNone && left.block) {
        chainNext = left.chain;
        if (ownBodyLines(ln, left.opener)) return;
      }
    }
    while (args && i < e && all[i] == '[') {  // more arguments after a body's ']'
      Left left;
      u32 end = constructEnd(t, i, rawOf, left, true, false, &bm);
      if (end != kNone) {
        i = end;
        break;
      }
      chainNext = left.chain;
      if (left.block ? ownBodyLines(ln, left.opener) : ownLines(ln, i, e, left, true)) return;
    }
    while (i < e) {
      const char c = all[i];
      if (c == '\\') {
        i += i + 1 < e ? 2 : 1;
        continue;
      }
      if (!kInlineOpenerByte[(u8)c]) {
        i++;
        continue;
      }
      if (reverted(i)) {  // a literal opener (a code run is literal as a whole)
        do i++;
        while (c == '`' && i < e && all[i] == '`');
        continue;
      }
      Left left;
      u32 end = constructEnd(t, i, rawOf, left, false, false, &bm);
      if (end != kNone) {
        i = std::max(end, i + 1);
        continue;
      }
      if (left.opener == kNone) {
        i++;
        continue;
      }
      chainNext = left.chain;
      if (left.block ? ownBodyLines(ln, left.opener) : ownLines(ln, i, e, left)) return;
      // the opener is literal now: read the construct again
    }
  }

  // Look ahead from the construct at `start` on line L (left open at
  // `left.opener`) over the following lines up to its bound, in doubling
  // windows. True when it closes: the lines up to its closer are owned.
  bool ownLines(u32 L, u32 start, u32 e, const Left& left, bool args = false) {
    std::vector<Span> slices{{start, e}};
    std::vector<u32> sliceLine{L};
    u32 next = L + 1;
    bool bound = false;
    u32 unclosed = left.opener;  // what the widest window still left open
    for (size_t want = 2;; want *= 2) {
      while (slices.size() < want && !bound) {
        if (next >= nlines) {
          bound = true;
          break;
        }
        u32 ls = lineStart(next), le = lineEnd(next), p = ls, c = 0;
        bool blank = isBlank(ls, le);
        if (matchContainers(le, p, c, blank) < open.size() || (blank && !left.container)) {
          bound = true;
          break;
        }
        if (!blank) {
          skipBlanks(le, p, c);
          slices.push_back({p, trimEnd(p, le)});
          sliceLine.push_back(next);
        }
        next++;
      }
      if (slices.size() == 1) break;  // bound at its own line: the line scan's reading stands
      LeafText view(all, slices);
      Left l2;
      u32 end = constructEnd(view.text(), 0, [&](u32 v) { return view.raw(v); }, l2, args);
      if (end != kNone || l2.block) {
        // closed — or its call closed and a block-form body opens: own the
        // lines up to there and resume the scan on the last of them
        const u32 at = end != kNone ? end : l2.opener;
        const u32 raw = view.raw(at);
        size_t k = 0;
        while (k + 1 < slices.size() && slices[k + 1].start <= raw) k++;
        if (sliceLine[k] == L) break;  // cannot happen: it did not close on its own line
        owning = true;
        ownBlock = false;
        ownUntil = sliceLine[k];
        ownResume = raw;
        resumeAtBody = end == kNone;
        if (resumeAtBody) chainNext = l2.chain;
        return true;
      }
      if (l2.opener != kNone) unclosed = view.raw(l2.opener);
      if (bound) break;
    }
    makeLiteral(unclosed, L, next);
    return false;
  }

  // A block-form body opened by the '[' at `open` (ending line L): it closes
  // at the first line whose first non-blank character is ']' at or left of
  // line L's indent, before container exit.
  bool ownBodyLines(u32 L, u32 openRaw) {
    u32 indent = 0;
    for (size_t k = 0; k < leafLines.size(); k++)
      if (leafLines[k] == L) indent = leafCols[k];
    u32 l = L + 1;
    for (; l < nlines; l++) {
      u32 ls = lineStart(l), le = lineEnd(l), p = ls, c = 0;
      bool blank = isBlank(ls, le);
      if (matchContainers(le, p, c, blank) < open.size()) break;
      skipBlanks(le, p, c);
      if (p < le && all[p] == ']' && c <= indent) {
        owning = true;
        ownBlock = true;
        ownUntil = l;
        blockBody = {openRaw, p};
        return true;
      }
    }
    makeLiteral(openRaw, L, l);
    return false;
  }

  // A line the paragraph owns: appended, no block starts on it.
  u32 continueLeaf(u32 ln, u32 pos, u32 col, u32 le, bool blank) {
    if (!blank) extend(le);
    const bool last = ln == ownUntil;
    if (ownBlock) {
      if (!last) {
        pushLeafLine(ln, {pos, le}, col);  // a body line keeps its indentation
        return ln + 1;
      }
      owning = false;
      leaf->bodies.push_back(blockBody);
      const u32 e = trimEnd(pos, le);
      pushLeafLine(ln, {pos, e}, col);
      scanLine(ln, blockBody.end + 1, e, /*args=*/true);
      return ln + 1;
    }
    if (blank) return ln + 1;  // a comment owns blank lines; they add nothing
    skipBlanks(le, pos, col);
    const u32 e = trimEnd(pos, le);
    pushLeafLine(ln, {pos, e}, col);
    if (last) {
      owning = false;
      scanLine(ln, ownResume, e);
    }
    return ln + 1;
  }

  // nesting-aware scan for the '--%' that closes a comment: one past it, or
  // kNone with `depth` updated
  u32 scanComment(u32 p, u32 e, int& depth) const {
    while (p < e) {
      if (p + 2 < e && all[p] == '%' && all[p + 1] == '-' && all[p + 2] == '-') {
        depth++;
        p += 3;
        continue;
      }
      if (p + 2 < e && all[p] == '-' && all[p + 1] == '-' && all[p + 2] == '%') {
        p += 3;
        if (--depth == 0) return p;
        continue;
      }
      p++;
    }
    return kNone;
  }

  // A fence: N >= 3 backticks; the closer needs >= N and only trailing
  // blanks. Content lines are dedented by the opener's indentation relative
  // to its container's content column; container exit ends the fence.
  void openFence(u32 pos, u32 indent, u32 le) {
    closeLeaf();
    u32 n = 0;
    while (pos + n < le && all[pos + n] == '`') n++;
    SkelNode* f = mk(SkelKind::Fence);
    f->span = {pos, le};
    u32 l0 = pos + n;
    while (l0 < le && all[l0] == ' ') l0++;
    f->langSpan = {l0, le};
    for (const OpenC& c : open) f->contained = f->contained || c.shape != Shape::Explicit;
    parent()->kids.push_back(f);
    carry = Carry::Fence;
    carryNode = f;
    carryDepth = open.size();
    fenceTicks = n;
    fenceIndent = indent;
  }
  void fenceLine(u32 pos, u32 col, u32 le) {
    SkelNode* f = carryNode;
    u32 q = pos, qc = col;
    skipBlanks(le, q, qc);
    u32 t = 0;
    while (q + t < le && all[q + t] == '`') t++;
    if (t >= fenceTicks && isBlank(q + t, le)) {
      f->span.end = le;
      carry = Carry::None;
      return;
    }
    u32 p = pos, c = col;
    skipBlanks(le, p, c, col + fenceIndent);
    f->lineSpans.push_back({p, le});
    f->span.end = le;
  }

  // A block comment from '%--' at `pos`; it may close on its own line (the
  // remainder of the line re-enters) or carry on.
  u32 blockComment(u32 ln, u32 pos, u32 le) {
    closeLeaf();
    SkelNode* cm = mk(SkelKind::Comment);
    cm->span = {pos, le};
    cm->inner = {pos + 3, le};
    parent()->kids.push_back(cm);
    int depth = 1;
    u32 end = scanComment(pos + 3, le, depth);
    if (end != kNone) return closeComment(ln, cm, pos + 3, end, le);
    cm->lineSpans.push_back({pos + 3, le});
    carry = Carry::Comment;
    carryNode = cm;
    carryDepth = open.size();
    commentDepth = depth;
    return ln + 1;
  }
  u32 closeComment(u32 ln, SkelNode* cm, u32 from, u32 end, u32 le) {
    cm->lineSpans.push_back({from, end - 3});
    cm->span.end = end;
    cm->inner.end = end - 3;
    carry = Carry::None;
    return remainder(ln, end, le);
  }

  // container exit (or EOF) before a carry's closer
  void endCarry() {
    if (carry == Carry::Fence)
      diags.add(Sev::Error, "parse-block", carryNode->span, "unterminated fence");
    else if (carry == Carry::Comment)
      diags.add(Sev::Error, "parse-block", carryNode->span, "unterminated comment");
    carry = Carry::None;
  }

  u32 carryLine(u32 ln, u32 pos, u32 col, u32 le) {
    switch (carry) {
      case Carry::Fence:
        fenceLine(pos, col, le);
        return ln + 1;
      case Carry::Comment: {
        int depth = commentDepth;
        u32 end = scanComment(pos, le, depth);
        if (end != kNone) return closeComment(ln, carryNode, pos, end, le);
        carryNode->lineSpans.push_back({pos, le});
        carryNode->span.end = le;
        carryNode->inner.end = le;
        commentDepth = depth;
        return ln + 1;
      }
      case Carry::None:
        break;
    }
    return ln + 1;
  }

  // A statement — '#let …' to the end of its line or a ';' at depth 0,
  // '#{…}' balanced — may continue over the following lines of its
  // containers (to container exit; EOF at the root). The lines are joined
  // structurally and scanned in growing windows, so a statement costs time in
  // proportion to its own length. A broken statement is an Error block, never
  // pasted JS.
  u32 statement(u32 ln, u32 pos, u32 le, bool let) {
    closeLeaf();
    std::vector<Span> slices{{pos, le}};
    std::vector<u32> sliceLine{ln};
    u32 next = ln + 1;
    bool exhausted = false;  // container exit or EOF reached
    for (size_t want = 2;; want *= 2) {
      while (slices.size() < want && !exhausted) {
        if (next >= nlines) {
          exhausted = true;
          break;
        }
        u32 ls = lineStart(next), le2 = lineEnd(next), p = ls, c = 0;
        bool blank = isBlank(ls, le2);
        if (matchContainers(le2, p, c, blank) < open.size()) {
          exhausted = true;
          break;
        }
        u32 q = p, qc = c;
        skipBlanks(le2, q, qc);
        std::string_view name = closerName(q, le2);
        if (!name.empty() && openRegion(name)) {
          exhausted = true;
          break;
        }
        slices.push_back({p, le2});
        sliceLine.push_back(next++);
      }
      LeafText view(all, slices);
      // `#let x = [ … ]`: a content literal (plan P2-12) — its body is
      // markup, matched as a content body's brackets (atoms skipped)
      if (let) {
        const std::string_view vt = view.text();
        const JsSimpleLet sl = jsSimpleLet(vt.substr(4));
        u32 open = 4 + sl.exprStart;
        while (open < vt.size() && (vt[open] == ' ' || vt[open] == '\t')) open++;
        if (sl.ok && open < vt.size() && vt[open] == '[') {
          BracketMatcher bm(vt);
          const i32 close = bm.body(open);
          if (close < 0 && !exhausted) continue;
          if (close >= 0) {
            const u32 end = view.raw((u32)close + 1);
            SkelNode* c = mk(SkelKind::CodeLet);
            c->span = {pos, end};
            c->inner = {view.raw(4 + sl.identStart), view.raw(4 + sl.identEnd)};  // its name
            c->content = true;
            // the body's lines, '[' and ']' excluded
            const u32 bs = view.raw(open + 1), be = view.raw((u32)close);
            for (const Span& sl2 : slices) {
              const u32 a = std::max(sl2.start, bs), b2 = std::min(sl2.end, be);
              if (sl2.end >= bs && sl2.start <= be) c->lineSpans.push_back({a, std::max(a, b2)});
            }
            parent()->kids.push_back(c);
            size_t k = 0;
            for (u32 at = 0; k + 1 < slices.size(); k++) {
              at += (slices[k].end - slices[k].start) + 1;
              if ((u32)close + 1 < at) break;
            }
            for (size_t l = 1; l <= k; l++) extend(slices[l].end);
            return remainder(sliceLine[k], end, lineEnd(sliceLine[k]));
          }
        }
      }
      JsScan s = scanJs(view.text(), let ? 4 : 1, !let);
      if (!s.ok) {
        bool ranOut = s.err && std::string_view(s.err) == "unterminated";
        if (ranOut && !exhausted) continue;
        u32 last = recoverStatement(ln);
        for (u32 l = ln + 1; l <= last; l++) extend(lineEnd(l));
        errorBlock({pos, lineEnd(last)}, "statement-unclosed",
                   let ? "unterminated #let: dropped up to the next blank line"
                       : "unterminated #{ block: dropped up to the next blank line");
        return last + 1;
      }
      // the slice holding the end
      size_t k = 0;
      for (u32 at = 0; k + 1 < slices.size(); k++) {
        at += (slices[k].end - slices[k].start) + 1;
        if (s.end < at) break;
      }
      const u32 end = view.raw(s.end);
      SkelNode* c = mk(let ? SkelKind::CodeLet : SkelKind::CodeBlock);
      c->span = {pos, end};
      if (let) c->inner = {pos + 4, s.hitSemicolon ? view.raw(s.end - 1) : end};
      else c->inner = {pos + 2, view.raw(s.end - 1)};
      // its lines, container prefixes stripped (a statement in a quote)
      for (const Span& sl2 : slices)
        if (sl2.end >= c->inner.start && sl2.start <= c->inner.end) {
          const u32 a = std::max(sl2.start, c->inner.start);
          c->lineSpans.push_back({a, std::max(a, std::min(sl2.end, c->inner.end))});
        }
      parent()->kids.push_back(c);
      for (size_t l = 1; l <= k; l++) extend(slices[l].end);
      return remainder(sliceLine[k], end, lineEnd(sliceLine[k]));
    }
  }

  // The rest of a line after a construct closed mid-line ('--%', '}', ';'):
  // another comment or statement, else paragraph text.
  u32 remainder(u32 ln, u32 at, u32 le) {
    u32 p = at, c = 0;
    skipBlanks(le, p, c);
    if (isBlank(p, le)) return ln + 1;
    std::string_view rest = all.substr(p, le - p);
    if (starts(rest, "%--")) return blockComment(ln, p, le);
    if (isLet(rest)) return statement(ln, p, le, true);
    if (starts(rest, "#{")) return statement(ln, p, le, false);
    addParaLine(ln, p, c, le);
    return ln + 1;
  }

  // A line's leaf content from `pos` (after its containers and starters);
  // returns the next line to process.
  u32 leafLine(u32 ln, u32 pos, u32 col, u32 le) {
    const u32 baseCol = col;
    skipBlanks(le, pos, col);
    if (pos >= le || isBlank(pos, le)) {
      closeLeaf();
      return ln + 1;
    }
    std::string_view rest = all.substr(pos, le - pos);

    if (starts(rest, "```")) {
      openFence(pos, col - baseCol, le);
      return ln + 1;
    }
    if (starts(rest, "%--")) {
      // a comment line inside a paragraph is part of it: an inline comment,
      // which owns the following lines until its closer
      if (leaf) {
        addParaLine(ln, pos, col, le);
        return ln + 1;
      }
      return blockComment(ln, pos, le);
    }
    if (rest[0] == '=') {
      u32 n = 0;
      while (n < rest.size() && rest[n] == '=') n++;
      if (n <= 6 && n < rest.size() && rest[n] == ' ') {
        closeLeaf();
        SkelNode* h = mk(SkelKind::Heading);
        h->level = (u8)n;
        u32 cs = pos + n + 1;
        u32 e = le;
        while (e > cs && (all[e - 1] == ' ' || all[e - 1] == '\r')) e--;
        // a trailing ` <id>` label (v2 §11.1; the one label grammar, plan P2-06)
        if (const LabelSuffix ls = trailingLabel(all, cs, e); ls.ok) {
          h->labelSpan = {ls.labelStart, ls.labelEnd};
          e = ls.textEnd;
        }
        h->span = {pos, e};
        h->lineSpans.push_back({cs, e});
        parent()->kids.push_back(h);
        return ln + 1;
      }
    }
    {  // thematic break: 3+ dashes alone
      u32 n = 0;
      while (n < rest.size() && rest[n] == '-') n++;
      if (n >= 3 && isBlank(pos + n, le)) {
        closeLeaf();
        SkelNode* r = mk(SkelKind::Rule);
        r->span = {pos, le};
        parent()->kids.push_back(r);
        return ln + 1;
      }
    }
    // region opener: #!name(args)? alone on its line (v2 §4.1)
    if (rest.size() >= 3 && rest[0] == '#' && rest[1] == '!' && isIdentStart(rest[2])) {
      u32 np = pos + 2;
      while (np < le && isIdentCont(all[np])) np++;
      Span argsSpan{np, np};
      u32 after = np;
      bool ok = true;
      if (after < le && all[after] == '(') {
        JsScan js = scanJs(all.substr(0, le), after, true);
        if (js.ok) {
          argsSpan = {after + 1, js.end - 1};
          after = js.end;
        } else {
          ok = false;
        }
      }
      // ` <id>` may close the opener line (plan P2-06)
      const LabelSuffix ls = ok ? trailingLabel(all, after, le) : LabelSuffix{};
      if (ok && isBlank(after, ls.ok ? ls.textEnd : le) && nestFull()) {
        errorBlock({pos, le}, "nest-limit",
                   "regions nest more than " + std::to_string(kMaxNesting) + " deep: this one is not opened");
        return ln + 1;
      }
      if (ok && isBlank(after, ls.ok ? ls.textEnd : le)) {
        closeLeaf();
        SkelNode* rg = mk(SkelKind::Region);
        rg->span = {pos, le};
        rg->langSpan = {pos + 2, np};
        rg->inner = argsSpan;
        if (ls.ok) rg->labelSpan = {ls.labelStart, ls.labelEnd};
        parent()->kids.push_back(rg);
        open.push_back({rg, Shape::Explicit});
        return ln + 1;
      }
      // fall through: not a region opener, plain paragraph text — say why
      if (ok && after < le)
        diags.add(Sev::Warning, "header-trailing", {after, le},
                  "text after '#!" + std::string(all.substr(pos + 2, np - pos - 2)) +
                      "(…)' makes this line a paragraph, not a region opener (a label is ' <id>')");
    }
    // a region closer with no open region of its name is an error block,
    // never a splice (App B rule 5; matched closers: processLine)
    if (std::string_view name = closerName(pos, le); !name.empty()) {
      errorBlock({pos, le}, "region-orphan",
                 "closer '#" + std::string(name) + "!' has no open region '#!" + std::string(name) + "'");
      return ln + 1;
    }
    if (isLet(rest)) return statement(ln, pos, le, true);
    if (starts(rest, "#{")) return statement(ln, pos, le, false);
    addParaLine(ln, pos, col, le);
    return ln + 1;
  }

  u32 processLine(u32 ln) {
    const u32 ls = lineStart(ln), le = lineEnd(ln);
    const bool blank = isBlank(ls, le);
    u32 pos = ls, col = 0;
    const size_t matched = matchContainers(le, pos, col, blank);
    if (carry != Carry::None) {
      if (matched >= carryDepth) {
        if (!blank) extend(le);
        return carryLine(ln, pos, col, le);
      }
      endCarry();
    }
    if (leaf && owning) return continueLeaf(ln, pos, col, le, blank);  // owned: see ownLines
    if (matched < open.size()) closeTo(matched);  // a blank line ends a quote
    if (blank) {
      closeLeaf();
      return ln + 1;
    }
    // region closer: '#name!' alone on its line closes the innermost open
    // region of that name; regions opened inside it end unclosed
    {
      u32 q = pos, qc = col;
      skipBlanks(le, q, qc);
      std::string_view name = closerName(q, le);
      if (size_t ri = name.empty() ? 0 : openRegion(name)) {
        closeTo(ri);
        extend(le);
        open.pop_back();
        return ln + 1;
      }
    }
    extend(le);
    tryStarters(le, pos, col);
    return leafLine(ln, pos, col, le);
  }

  void run() {
    root = mk(SkelKind::Doc);
    root->span = lines.empty() ? Span{0, 0} : Span{lines.front().start, lines.back().end};
    nlines = (u32)lines.size();
    for (u32 ln = 0; ln < nlines;) ln = processLine(ln);
    if (carry != Carry::None) endCarry();
    for (const OpenC& c : open)
      if (c.shape == Shape::Explicit) regionUnclosed(c.node);
  }
};

}  // namespace

Skeleton linepassLines(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                       DiagSink& diags, u32 depth) {
  LinePass lp{src, lines, arena, diags, src.view(), depth};
  lp.run();
  return {lp.root, std::move(lp.windows)};
}

Skeleton linepass(const SourceText& src, Arena& arena, DiagSink& diags, const FrontEndOptions& opts) {
  std::vector<Span> lines(src.lineCount());
  for (u32 l = 0; l < src.lineCount(); l++) lines[l] = {src.lineStart(l), src.lineEnd(l)};
  // (plan P3-35; D-L09) front matter: with the host's option, a `---` line at
  // offset 0 through the next `---` or `...` line is one comment block;
  // unclosed, it is markup as usual
  SkelNode* front = nullptr;
  size_t first = 0;
  auto lineIs = [&](size_t l, std::string_view w) {
    std::string_view t = src.slice(lines[l]);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.remove_suffix(1);
    return t == w;
  };
  if (opts.frontMatter && lines.size() > 1 && lines[0].start == 0 && lineIs(0, "---"))
    for (size_t l = 1; l < lines.size(); l++)
      if (lineIs(l, "---") || lineIs(l, "...")) {
        front = arena.make<SkelNode>();
        front->kind = SkelKind::Comment;
        front->front = true;
        front->span = {0, lines[l].end};
        front->lineSpans.assign(lines.begin() + 1, lines.begin() + (long)l);
        first = l + 1;
        break;
      }
  Skeleton sk = linepassLines(src, std::vector<Span>(lines.begin() + (long)first, lines.end()), arena, diags);
  if (front) sk.root->kids.insert(sk.root->kids.begin(), front);
  sk.root->span = {0, src.size()};
  return sk;
}

static void dumpNode(std::string& out, const SkelNode* n, const SourceText& src, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  switch (n->kind) {
    case SkelKind::Doc: appendf(out, "doc @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::Para:
      appendf(out, "para @[%u,%u) lines=%zu\n", n->span.start, n->span.end, n->lineSpans.size());
      break;
    case SkelKind::Heading:
      appendf(out, "heading%d @[%u,%u)", n->level, n->span.start, n->span.end);
      if (!n->labelSpan.empty()) {
        out += " label=\"";
        appendEscaped(out, src.slice(n->labelSpan));
        out += "\"";
      }
      out += "\n";
      break;
    case SkelKind::List:
      if (n->marker == '/') {  // (plan P3-34) a description list
        appendf(out, "terms @[%u,%u)\n", n->span.start, n->span.end);
        break;
      }
      appendf(out, "list %s start=%d @[%u,%u)\n", n->ordered ? "ordered" : "bullet", n->start,
              n->span.start, n->span.end);
      break;
    case SkelKind::Item:
      appendf(out, "item @[%u,%u)", n->span.start, n->span.end);
      if (!n->termSpan.empty()) appendf(out, " term=@[%u,%u)", n->termSpan.start, n->termSpan.end);
      out += "\n";
      break;
    case SkelKind::Quote: appendf(out, "quote @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::Fence: {
      appendf(out, "fence @[%u,%u) lang=\"", n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->langSpan));
      appendf(out, "\" lines=%zu\n", n->lineSpans.size());
      break;
    }
    case SkelKind::Rule: appendf(out, "rule @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::CodeLet:
    case SkelKind::CodeBlock:
      appendf(out, "%s @[%u,%u) js=\"", n->kind == SkelKind::CodeLet ? "code-let" : "code-block",
              n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->inner));
      out += "\"\n";
      break;
    case SkelKind::Comment:
      appendf(out, "comment @[%u,%u)\n", n->span.start, n->span.end);
      break;
    case SkelKind::Error:
      appendf(out, "error @[%u,%u) code=%s\n", n->span.start, n->span.end, n->errCode);
      break;
    case SkelKind::Region:
      appendf(out, "region @[%u,%u) name=\"", n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->langSpan));
      out += "\"\n";
      break;
  }
  for (const SkelNode* k : n->kids) dumpNode(out, k, src, depth + 1);
}

std::string dumpSkeleton(const Skeleton& sk, const SourceText& src) {
  std::string out;
  if (sk.root) dumpNode(out, sk.root, src, 0);
  return out;
}

}  // namespace tsr
