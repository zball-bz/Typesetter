#include "env.h"

#include <algorithm>

#include "dict.h"

namespace tsr {

namespace {
const ArgVal* ext(const Decl& d, const Interner& strs, std::string_view name) {
  for (const ArgVal& a : d.args)
    if (a.key == ArgK::ext && strs.get(a.name) == name) return &a;
  return nullptr;
}
std::string_view extStr(const Decl& d, const Interner& strs, std::string_view name) {
  const ArgVal* a = ext(d, strs, name);
  return a && a->tag == ArgTag::Str ? strs.get(a->ref) : std::string_view{};
}
bool extBool(const Decl& d, const Interner& strs, std::string_view name) {
  const ArgVal* a = ext(d, strs, name);
  return a && a->tag == ArgTag::Bool && a->num != 0;
}

// a declarable name: letters, dotted segments of letters (std. is reserved)
bool nameOk(std::string_view n) {
  if (n.empty() || n.front() == '.' || n.back() == '.') return false;
  for (size_t i = 0; i < n.size(); i++) {
    const char c = n[i];
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!letter && !(c == '.' && n[i - 1] != '.')) return false;
  }
  return true;
}

constexpr const char* kClassNames[] = {"ord", "op", "bin", "rel", "open", "close", "punct", "inner"};
}  // namespace

void MathEnv::build(const std::vector<Decl>& decls, const Interner& strs, DiagSink& diags) {
  rows_.clear();
  std::vector<const Decl*> positional;
  for (const Decl& d : decls)
    if (d.type && !kDecls[d.type].hoisted) positional.push_back(&d);
  std::stable_sort(positional.begin(), positional.end(),
                   [](const Decl* a, const Decl* b) { return a->flowIndex < b->flowIndex; });
  for (size_t i = 0; i < positional.size(); i++) {
    const Decl& d = *positional[i];
    const std::string_view type = kDecls[d.type].name;
    MathDeclRow r;
    if (type == "math.symbol") r.k = MathDeclRow::Symbol;
    else if (type == "math.op") r.k = MathDeclRow::Op;
    else if (type == "math.fn") r.k = MathDeclRow::Fn;
    else continue;  // another positional type (rule: P3)
    r.name = std::string(strs.get(d.name));
    r.epoch = (u32)i + 1;
    auto bad = [&](const std::string& why) { diags.add(Sev::Warning, "math-decl", d.span, std::string(type) + " '" + r.name + "': " + why); };
    if (r.name.rfind("std.", 0) == 0) {
      bad("names starting with std. are the built-ins'");
      continue;
    }
    if (!nameOk(r.name)) {
      bad("a name is letters, or dotted words of letters");
      continue;
    }
    if (MathDict::byName(r.name) || mathRow(r.name))
      diags.add(Sev::Info, "math-shadow", d.span,
                std::string(type) + " '" + r.name + "' shadows the built-in from here on (std." + r.name + " keeps it)");
    switch (r.k) {
      case MathDeclRow::Symbol: {
        const std::string_view ch = extStr(d, strs, "char");
        u32 at = 0;
        const u32 cp = ch.empty() ? 0 : utf8Next(ch, at);
        if (!cp || at != ch.size() || cp > 0x10FFFF) {
          bad("char is one character");
          continue;
        }
        r.cp = cp;
        r.cls = MathDict::classOfCp(cp);
        if (const std::string_view c = extStr(d, strs, "class"); !c.empty()) {
          const auto* hit = std::find(std::begin(kClassNames), std::end(kClassNames), c);
          if (hit == std::end(kClassNames)) {
            bad("class is ord, op, bin, rel, open, close, punct or inner");
            continue;
          }
          r.cls = (u8)(hit - std::begin(kClassNames));
        }
        r.claimCp = extBool(d, strs, "claim-cp");
        break;
      }
      case MathDeclRow::Op: {
        const std::string_view lim = extStr(d, strs, "limits");
        if (!lim.empty() && lim != "display" && lim != "always" && lim != "never") {
          bad("limits is display, always or never");
          continue;
        }
        r.flags = kFlagTextOp | (lim == "display" || lim == "always" ? kFlagLimits : 0);
        break;
      }
      case MathDeclRow::Fn: {
        r.row.name = r.name;
        std::string_view ps = extStr(d, strs, "params");
        while (!ps.empty()) {
          const size_t comma = ps.find(',');
          r.row.params.push_back(parseSlotSpec(ps.substr(0, comma)));  // (plan P3-29: `r: rows`, `r: cells`)
          ps = comma == std::string_view::npos ? std::string_view{} : ps.substr(comma + 1);
        }
        bool paramsOk = r.row.params.size() <= 9;
        for (const SlotSpec& sp : r.row.params) paramsOk = paramsOk && nameOk(sp.name) && sp.name.find('.') == std::string::npos;
        if (!paramsOk) {
          bad("params are at most 9 names of letters");
          continue;
        }
        // the body binds against the declarations before it
        const MathScope before{this, r.epoch - 1};
        std::vector<MathDiag> bd;
        r.row.body = parseTemplateBody(extStr(d, strs, "body"), r.row.params, arena_, &before, &bd);
        std::string why;
        if (!checkRow(r.row, why)) {
          bad(why);
          continue;
        }
        if (const std::string_view bare = extStr(d, strs, "bare"); !bare.empty()) {
          if (const MathDeclRow* s = find(bare, r.epoch - 1); s && s->k == MathDeclRow::Symbol) {
            r.row.bareCp = s->cp;
            r.row.bareCls = s->cls;
          } else if (const SymbolInfo* e = MathDict::byName(bare)) {
            r.row.bareCp = e->cp;
            r.row.bareCls = e->cls;
          } else {
            u32 at = 0;
            r.row.bareCp = utf8Next(bare, at);
            r.row.bareCls = MathDict::classOfCp(r.row.bareCp);
          }
        }
        break;
      }
    }
    rows_.push_back(std::move(r));
  }
}

const MathDeclRow* MathEnv::find(std::string_view name, u32 epoch) const {
  for (size_t i = rows_.size(); i-- > 0;)
    if (rows_[i].epoch <= epoch && rows_[i].name == name) return &rows_[i];
  return nullptr;
}

const MathDeclRow* MathEnv::claimed(u32 cp, u32 epoch) const {
  for (size_t i = rows_.size(); i-- > 0;)
    if (rows_[i].epoch <= epoch && rows_[i].k == MathDeclRow::Symbol && rows_[i].claimCp && rows_[i].cp == cp)
      return &rows_[i];
  return nullptr;
}

// ---- a formula's source (plan P2-15) ----------------------------------------

namespace {
// the sentinels and control bytes a value cannot carry into the lexer
void appendClean(std::string& out, std::string_view s) {
  for (char c : s) out += (u8)c < 0x07 ? ' ' : c;
}
void plainText(const ContentNode* n, const Interner& strs, std::string& out) {
  if (n->kind == Kind::text) out += strs.get(n->str);
  for (const ContentNode* k : n->kids) plainText(k, strs, out);
}
bool isFormula(const ContentNode* n) { return n->kind == Kind::mathinline || n->kind == Kind::mathblock; }

void build(const ContentNode* n, const Interner& strs, DiagSink* diags, MathSource& m, int depth) {
  if (const StrRef src = attrStr(n, ArgK::src)) {  // the old form: its source
    appendClean(m.text, strs.get(src));
    m.copy += strs.get(src);
    return;
  }
  bool prevFragment = false;
  for (const ContentNode* k : n->kids) {
    if (slotOn(slotOf(k, strs), n->kind)) continue;  // a part (its tag), not its source
    if (k->kind == Kind::mathsrc) {
      if (prevFragment) {
        m.text += '\n';
        m.copy += '\n';
      }
      m.map.insert(m.map.end(), {(u32)m.text.size(), k->span.start, k->span.end});
      const std::string_view s = strs.get(attrStr(k, ArgK::src));
      appendClean(m.text, s);
      m.copy += s;
      prevFragment = true;
      continue;
    }
    prevFragment = false;
    m.map.insert(m.map.end(), {(u32)m.text.size(), k->span.start, k->span.end});
    // a styled or plain group of one formula is that formula (its paint
    // properties wait for content in formulas, P4)
    const ContentNode* v = k;
    while ((v->kind == Kind::seq || v->kind == Kind::styled) && v->kids.size() == 1) v = v->kids[0];
    if (isFormula(v) && depth < 32) {
      m.text += '\x01';
      build(v, strs, diags, m, depth + 1);
      m.text += '\x02';
    } else if (v->kind == Kind::error) {
      m.text += '\x05';
      appendClean(m.text, strs.get(attrStr(v, ArgK::message)));
      m.text += '\x06';
      m.copy += "\xE2\x9A\xA0";
    } else if (levelOf(v->kind) == Level::Block || isFormula(v)) {
      if (diags)
        diags->add(Sev::Warning, "math-hole-kind", k->span,
                   std::string("a ") + kindName(v->kind) + " cannot stand in a formula");
      m.text += '\x05';
      m.text += kindName(v->kind);
      m.text += '\x06';
    } else {
      // a string, or other inline content set as its text (content in a
      // formula — a reference, a note marker — is P4's)
      std::string t;
      plainText(v, strs, t);
      if (v->kind != Kind::text && diags)
        diags->add(Sev::Info, "math-hole-kind", k->span,
                   std::string("a ") + kindName(v->kind) + " in a formula is set as its text");
      m.text += '\x03';
      appendClean(m.text, t);
      m.text += '\x04';
      m.copy += "\"" + t + "\"";
    }
  }
}
}  // namespace

StrRef mathSourceRef(const ContentNode* n, const Interner& strs) {
  StrRef r = attrStr(n, ArgK::src);
  if (!r) {  // one clean fragment, its parts (a tag) aside
    const ContentNode* only = nullptr;
    for (const ContentNode* k : n->kids) {
      if (slotOn(slotOf(k, strs), n->kind)) continue;
      if (only || k->kind != Kind::mathsrc) return 0;
      only = k;
    }
    if (!only) return 0;
    r = attrStr(only, ArgK::src);
  }
  for (char c : strs.get(r))
    if ((u8)c < 0x07) return 0;
  return r;
}

MathSource mathSource(const ContentNode* n, const Interner& strs, DiagSink* diags) {
  MathSource m;
  build(n, strs, diags, m, 0);
  return m;
}

}  // namespace tsr
