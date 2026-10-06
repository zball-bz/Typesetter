#include "hlist.h"

#include <cmath>

#include "objects.h"
#include "textrules.h"

namespace tsr {

namespace {

const char* rcName(RealizeClass rc) {
  switch (rc) {
    case RealizeClass::Plain: return "plain";
    case RealizeClass::LetterSpaced: return "letter-spaced";
    case RealizeClass::BlankBearing: return "blank-bearing";
    case RealizeClass::Pinned: return "pinned";
    case RealizeClass::Rigid: return "rigid";
    case RealizeClass::Object: return "object";
  }
  return "?";
}

const char* gcName(u8 gc) {
  switch ((GC)gc) {
    case GC::Word: return "word";
    case GC::InterChar: return "inter";
    case GC::Autospace: return "autospace";
    case GC::Blank: return "blank";
    case GC::ObjectSpace: return "objspace";
    case GC::Fill: return "fill";
  }
  return "?";
}

void pen(std::string& out, float x) {
  if (!(x < kPenInf)) out += "INF";
  else if (x <= -kPenInf) out += "-INF";  // forced (a hard line break)
  else appendf(out, "%g", (double)x);
}

void quoted(std::string& out, const Interner& strs, StrRef r) {
  out += '"';
  appendEscaped(out, strs.get(r));
  out += '"';
}

}  // namespace

void dumpHList(std::string& out, const HList& h, const Interner& strs, const StyleTable& styles,
               const char* indent) {
  auto tail = [&](const HItem& it) {
    if (it.attrs & IA_SourceSpace) out += " src";
    if (it.attrs & IA_OwnedByNext) out += " owned-by-next";
    if (it.attrs & IA_Displaced) out += " displaced";
    const ColdRec& c = h.cold[it.cold];
    if (it.attrs & IA_Anchor) {
      out += " anchor=";
      quoted(out, strs, c.anchor);
    }
    appendf(out, " r%u @[%u,%u)\n", it.run, c.srcStart, c.srcEnd);
  };
  // (plan P4-08) its justification unit
  if (!h.items.empty()) appendf(out, "%sju=%dsu\n", indent, h.juSu);
  for (const HItem& it : h.items) {
    out += indent;
    switch (it.k) {
      case IK::Box: {
        const RunRec& r = h.runs[it.run];
        const AdvanceSpec& sp = h.specs[it.aux];
        if (r.rc == RealizeClass::Object) {
          const ObjPart& pt = h.parts[sp.obj];
          const InlineObject& ob = h.objs[pt.obj];
          appendf(out, "box object o%u %s", pt.obj, objectKind(ob.kind).name);
          if (ob.deferred) out += " placeholder";
          else appendf(out, " part=%u asc=%dsu desc=%dsu", sp.obj - ob.part0, pt.asc, pt.desc);
          if (sp.str) {
            out += ' ';
            quoted(out, strs, sp.str);
          }
        } else if (r.syn == SynKind::Indent) {
          out += "box indent";
        } else {
          appendf(out, "box %s ", kCCName[it.cls]);
          quoted(out, strs, sp.str);
        }
        appendf(out, " w=%dsu", it.w);
        if (r.rc == RealizeClass::LetterSpaced || (r.rc == RealizeClass::Pinned && r.syn != SynKind::Indent))
          appendf(out, " wt=%g", (double)it.x);
        if (r.rc == RealizeClass::BlankBearing) out += (kCCFlags[it.cls] & kCC_open) ? " blank=L" : " blank=R";
        if (sp.k == AdvanceSpec::Defined) appendf(out, " defined=%gem", sp.em);
        break;
      }
      case IK::Glue: {
        // (plan P4-08) its stretch = shrink: the weight times the unit
        appendf(out, "glue %s w=%dsu x=%g", gcName(it.cls), it.w, (double)it.x);
        if (it.x > 0 && it.cls != (u8)GC::Fill) appendf(out, " st=%dsu", (Su)std::lround((double)it.x * h.juSu));
        if (it.cls == (u8)GC::InterChar) break;
        const AdvanceSpec& sp = h.specs[it.aux];
        if (sp.k == AdvanceSpec::KernCtx) {
          out += " kern=";
          quoted(out, strs, sp.tri);
        }
        break;
      }
      case IK::Penalty:
        out += "pen ";
        pen(out, it.x);
        appendf(out, " r%u\n", it.run);
        continue;
      case IK::Disc: {
        const DiscRec& d = h.discs[it.aux];
        out += "disc pen=";
        pen(out, it.x);
        appendf(out, " w=%dsu pre=", it.w);
        if (!d.preN) out += "none";  // (plan P4-06) an explicit hyphen's, an emergency break's
        for (u32 s = d.pre; s < d.pre + d.preN; s++) {
          quoted(out, strs, h.specs[h.side[s].aux].str);
          appendf(out, " %dsu", h.side[s].w);
        }
        if (d.spec != ~0u) {
          out += " kern=";
          quoted(out, strs, h.specs[d.spec].tri);
        }
        break;
      }
    }
    tail(it);
  }
  dumpObjects(out, h, strs, indent);
  for (size_t i = 0; i < h.runs.size(); i++) {
    const RunRec& r = h.runs[i];
    appendf(out, "%srun r%zu %s", indent, i, rcName(r.rc));
    if (r.syn == SynKind::Ref) out += " ref";
    if (r.syn == SynKind::Indent) out += " indent";
    const Styling& st = styles.get(r.face);
    if (st.weight == 700) out += " BOLD";
    if (st.italic) out += " EM";
    if (st.fontRole == FONTROLE_MONO) out += " CODE";
    if (r.link) out += " LINK";
    if (st.script == SCRIPT_CJK) out += " CJK";
    if (st.sizeMul != 1.0f) appendf(out, " x%.2f", (double)st.sizeMul);
    appendStyleFields(out, st, strs);
    if (r.link) {  // (an anchor as its href: plan P3-04)
      out += " link=\"";
      if (r.link.anchor) {
        out += '#';
        out += kAnchorPrefix;
      }
      appendEscaped(out, strs.get(r.link.ref));
      out += "\"";
    }
    if (r.anchor) {
      out += " anchor=";
      quoted(out, strs, r.anchor);
    }
    out += '\n';
  }
}

std::string lintHList(const HList& h) {
  std::string out;
  const std::vector<HItem>& v = h.items;
  const size_t n = v.size();
  // breakpoints per boundary (the items between two Boxes/Discs)
  auto breaksIn = [&](size_t from, size_t to) {  // [from, to)
    int c = 0;
    for (size_t i = from; i < to; i++) c += isBreakpoint(v, i) ? 1 : 0;
    return c;
  };
  size_t i = 0;
  while (i < n) {
    if (isBoxOrDisc(v[i])) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < n && !isBoxOrDisc(v[j])) j++;
    const int c = breaksIn(i, j);
    if (c > 1) appendf(out, "item %zu: %d breakpoints in one boundary\n", i, c);
    // kinsoku: no break after an opening glyph, none before a closing one
    const bool afterOpen = i > 0 && v[i - 1].k == IK::Box && h.runs[v[i - 1].run].rc == RealizeClass::BlankBearing &&
                           (kCCFlags[v[i - 1].cls] & kCC_open);
    // (plan P4-05) before any non-starter — a closer, a stop, a small kana,
    // an iteration mark (the rules' nostart column)
    const bool beforeClose = j < n && v[j].k == IK::Box && (kCCFlags[v[j].cls] & kCC_nostart);
    if (c > 0 && (afterOpen || beforeClose))
      appendf(out, "item %zu: a breakpoint %s\n", i,
              afterOpen ? "after an opening glyph" : "before a non-starter");
    i = j;
  }
  // runs: numbered in order, contiguous, every one used
  for (size_t k = 0; k < n; k++) {
    const u32 want = k == 0 ? 0 : v[k - 1].run;
    if (v[k].run != want && v[k].run != want + 1)
      appendf(out, "item %zu: run r%u after r%u\n", k, v[k].run, want);
    if (v[k].run >= h.runs.size()) appendf(out, "item %zu: run r%u out of range\n", k, v[k].run);
  }
  if (n && v[n - 1].run + 1 != h.runs.size()) appendf(out, "%zu runs, last used r%u\n", h.runs.size(), v[n - 1].run);
  // (plan P4-01) an anchor opens its run (after its glyph's leading blank
  // at most), which carries it: paint writes its id once, where the run starts
  for (size_t k = 0; k < n; k++) {
    if (!(v[k].attrs & IA_Anchor) || v[k].run >= h.runs.size()) continue;
    size_t first = k;
    while (first > 0 && v[first - 1].run == v[k].run) first--;
    bool opens = true;
    for (size_t m = first; m < k; m++) opens = opens && v[m].k == IK::Glue && v[m].cls == (u8)GC::Blank;
    if (!opens) appendf(out, "item %zu: an anchor inside run r%u\n", k, v[k].run);
    else if (h.runs[v[k].run].anchor != h.cold[v[k].cold].anchor)
      appendf(out, "item %zu: run r%u does not carry its anchor\n", k, v[k].run);
  }
  // (plan P4-01) a junction kern (KernCtx) only between two text boxes: the
  // browser kerns inside a text run, never at an inline block
  for (size_t k = 0; k < n; k++) {
    const bool ctx = (v[k].k == IK::Glue && v[k].cls != (u8)GC::InterChar && v[k].aux < h.specs.size() &&
                      h.specs[v[k].aux].k == AdvanceSpec::KernCtx) ||
                     (v[k].k == IK::Disc && h.discs[v[k].aux].spec != ~0u);
    if (!ctx) continue;
    auto textBox = [&](i64 m, int dir) {
      while (m >= 0 && m < (i64)n && v[m].k == IK::Penalty) m += dir;
      if (m < 0 || m >= (i64)n || v[m].k != IK::Box) return false;
      const RealizeClass rc = h.runs[v[m].run].rc;
      return rc == RealizeClass::Plain || rc == RealizeClass::Rigid;
    };
    if (!textBox((i64)k - 1, -1) || !textBox((i64)k + 1, 1))
      appendf(out, "item %zu: a junction kern beside no text box\n", k);
  }
  // run homogeneity
  for (size_t k = 0; k < n;) {
    size_t e = k;
    while (e < n && v[e].run == v[k].run) e++;
    if (v[k].run < h.runs.size()) {
      const RealizeClass rc = h.runs[v[k].run].rc;
      int boxes = 0;
      for (size_t m = k; m < e; m++) boxes += v[m].k == IK::Box ? 1 : 0;
      if ((rc == RealizeClass::BlankBearing || rc == RealizeClass::Pinned || rc == RealizeClass::Object) &&
          boxes > 1)
        appendf(out, "item %zu: %d boxes in a %s run\n", k, boxes, rcName(rc));
      if (rc == RealizeClass::LetterSpaced) {
        // every non-final box is followed by InterChar glue
        for (size_t m = k; m < e; m++) {
          if (v[m].k != IK::Box) continue;
          size_t nb = m + 1;
          while (nb < e && v[nb].k != IK::Box) nb++;
          if (nb >= e) break;
          bool gap = false;
          for (size_t q = m + 1; q < nb; q++) gap = gap || (v[q].k == IK::Glue && v[q].cls == (u8)GC::InterChar);
          if (!gap) appendf(out, "item %zu: letter-spaced box without InterChar glue after it\n", m);
        }
      }
    }
    k = e;
  }
  return out;
}

}  // namespace tsr
