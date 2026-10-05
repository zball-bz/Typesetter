#include "sidecars.h"

#include "../inline/fragment.h"

namespace tsr {

// verbatim-design §5: split each code line at the fence-declared marker;
// the code part re-forms the body (what the tokenizer will see), the
// comment part parses as an inline fragment into a trailing
// group{role:"sidecar-lines"} child — one seq per logical line.
void extractSidecars(ContentNode* n, Arena& arena, Interner& strs, StyleTable& styles,
                     DiagSink& diags) {
  if (!n) return;
  if (n->kind == Kind::codeblock && n->kids.size() == 1 &&
      n->kids[0]->kind == Kind::text) {
    StrRef mk = 0;
    for (const ArgVal& a : n->args)
      if (a.key == ArgK::sidecar && a.tag == ArgTag::Str) mk = a.ref;
    std::string marker(mk ? strs.get(mk) : std::string_view{});
    if (!marker.empty()) {
      std::string_view body = strs.get(n->kids[0]->str);
      StyleId st = n->kids[0]->style;
      Span sp = n->kids[0]->span;
      std::string codeBody;
      ContentNode* group = arena.make<ContentNode>();
      group->kind = Kind::group;
      group->span = sp;
      group->style = n->style;
      {
        ArgVal a;
        a.key = ArgK::role;
        a.tag = ArgTag::Str;
        a.ref = strs.intern("sidecar-lines");
        group->args.push_back(a);
      }
      bool any = false;
      size_t pos = 0;
      while (pos <= body.size()) {
        size_t eol = body.find('\n', pos);
        if (eol == std::string_view::npos) eol = body.size();
        std::string_view line = body.substr(pos, eol - pos);
        size_t cut = line.find(marker);
        ContentNode* seq = arena.make<ContentNode>();
        seq->kind = Kind::seq;
        seq->span = sp;
        seq->style = n->style;
        if (cut != std::string_view::npos) {
          std::string_view code = line.substr(0, cut);
          while (!code.empty() && (code.back() == ' ' || code.back() == '\t'))
            code.remove_suffix(1);
          std::string_view note = line.substr(cut + marker.size());
          while (!note.empty() && note.front() == ' ') note.remove_prefix(1);
          codeBody.append(code);
          if (!note.empty()) {
            auto ns2 = parseInlineFragment(note, st, sp, arena, strs, styles, diags);
            seq->kids.assign(ns2.begin(), ns2.end());
            any = true;
          }
        } else {
          codeBody.append(line);
        }
        group->kids.push_back(seq);
        if (eol == body.size()) break;
        codeBody += '\n';
        pos = eol + 1;
      }
      if (any) {
        n->kids[0]->str = strs.intern(codeBody);
        n->kids.push_back(group);
      }
    }
  }
  for (ContentNode* k : n->kids) extractSidecars(k, arena, strs, styles, diags);
}

}  // namespace tsr
