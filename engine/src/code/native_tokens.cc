// Native token provider (plan P1-03 moved it from engine/test into the
// engine's native build): statically linked tree-sitter + grammars, the same
// parse tables the web side modules are compiled from — highlight goldens
// are therefore byte-deterministic (code-design.md §2). Queries are embedded
// at build time (native_queries.gen.h), so tsrc needs no repository path.
// Priority contract (shared with runtime/src/worker/tokens.mjs): captures
// sort by (start asc, patternIndex asc); earlier pattern wins on overlap.
#include "native_tokens.h"

#include <tree_sitter/api.h>

#include <algorithm>

#include "native_queries.gen.h"

extern "C" const TSLanguage* tree_sitter_json();
extern "C" const TSLanguage* tree_sitter_tsm();

namespace tsr {

namespace {
const TSLanguage* nativeGrammar(std::string_view lang) {
  if (lang == "json") return tree_sitter_json();
  if (lang == "tsm") return tree_sitter_tsm();
  return nullptr;
}
std::string_view nativeQuery(std::string_view lang) {
  if (lang == "json") return kQueryJson;
  if (lang == "tsm") return kQueryTsm;
  return {};
}
}  // namespace

void provideNativeTokens(Doc& doc) {
  for (auto& req : doc.tokenReqs) {
    if (req.provided) continue;
    std::string lang(doc.strs.get(req.lang));
    std::string body(doc.strs.get(req.body));
    const TSLanguage* g = nativeGrammar(lang);
    if (!g) {
      doc.provideTokens(req.id, nullptr, 0);
      continue;
    }
    std::string_view scm = nativeQuery(lang);

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, g);
    TSTree* tree = ts_parser_parse_string(parser, nullptr, body.data(),
                                          (uint32_t)body.size());
    uint32_t eo = 0;
    TSQueryError et;
    TSQuery* q = ts_query_new(g, scm.data(), (uint32_t)scm.size(), &eo, &et);
    std::vector<CodeToken> out;
    if (q) {
      struct Cap { u32 s, e, pat; int tag; };
      std::vector<Cap> caps;
      TSQueryCursor* cur = ts_query_cursor_new();
      ts_query_cursor_exec(cur, q, ts_tree_root_node(tree));
      TSQueryMatch m;
      while (ts_query_cursor_next_match(cur, &m)) {
        for (u16 i = 0; i < m.capture_count; i++) {
          const TSQueryCapture& c = m.captures[i];
          uint32_t len = 0;
          const char* nm = ts_query_capture_name_for_id(q, c.index, &len);
          int tag = tokenTagFromCapture(std::string_view(nm, len));
          if (tag < 0) continue;
          caps.push_back({ts_node_start_byte(c.node), ts_node_end_byte(c.node),
                          m.pattern_index, tag});
        }
      }
      ts_query_cursor_delete(cur);
      std::sort(caps.begin(), caps.end(), [](const Cap& a, const Cap& b) {
        return a.s != b.s ? a.s < b.s : a.pat < b.pat;
      });
      u32 covered = 0;
      for (const Cap& c : caps) {
        if (c.s < covered || c.e <= c.s) continue;  // earlier pattern won
        out.push_back({c.s, c.e, (u8)c.tag});
        covered = c.e;
      }
      ts_query_delete(q);
    }
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    doc.provideTokens(req.id, out.data(), out.size());
  }
}

}  // namespace tsr
