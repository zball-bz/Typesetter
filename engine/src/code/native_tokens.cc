// Native token provider (plan P1-03 moved it from engine/test into the
// engine's native build): statically linked tree-sitter + grammars, the same
// parse tables the web side modules are compiled from — highlight goldens
// are therefore byte-deterministic (code-design.md §2). The grammars and
// their queries are engine/schema/languages.json's native ones, embedded at
// build time (native_queries.gen.h; plan P3-22), so tsrc needs no repository
// path.
// Priority contract (shared with runtime/src/shared/hl-core.mjs): captures
// sort stably by (start asc, patternIndex asc), ties in the cursor's order;
// an earlier capture wins an overlap. A match holds when its string
// predicates do (#eq?, #not-eq?, #any-of?, #not-any-of? — web-tree-sitter
// evaluates the same; gen-languages refuses a native grammar whose queries
// use a regular expression).
#include "native_tokens.h"

#include <tree_sitter/api.h>

#include <algorithm>

#include "native_queries.gen.h"

namespace tsr {

namespace {
const NativeGrammar* nativeGrammar(std::string_view lang) {
  for (const NativeGrammar& g : kNativeGrammars)
    if (g.name == lang) return &g;
  return nullptr;
}

// the text predicates of the match's pattern hold
bool predicatesHold(const TSQuery* q, const TSQueryMatch& m, std::string_view body) {
  uint32_t n = 0;
  const TSQueryPredicateStep* st = ts_query_predicates_for_pattern(q, m.pattern_index, &n);
  auto str = [&](uint32_t id) {
    uint32_t len = 0;
    const char* s = ts_query_string_value_for_id(q, id, &len);
    return std::string_view(s, len);
  };
  // an argument's text: a string, or its capture's first node (none: absent)
  auto text = [&](const TSQueryPredicateStep& a, std::string_view& out) {
    if (a.type == TSQueryPredicateStepTypeString) {
      out = str(a.value_id);
      return true;
    }
    for (u16 i = 0; i < m.capture_count; i++)
      if (m.captures[i].index == a.value_id) {
        const u32 s = ts_node_start_byte(m.captures[i].node), e = ts_node_end_byte(m.captures[i].node);
        out = body.substr(s, e - s);
        return true;
      }
    return false;
  };
  for (uint32_t i = 0; i < n;) {
    uint32_t j = i;
    while (j < n && st[j].type != TSQueryPredicateStepTypeDone) j++;
    if (j > i && st[i].type == TSQueryPredicateStepTypeString) {
      const std::string_view name = str(st[i].value_id);
      std::string_view subject;
      const bool has = j - i >= 2 && text(st[i + 1], subject);
      if ((name == "eq?" || name == "not-eq?") && has && j - i == 3) {
        std::string_view other;
        if (text(st[i + 2], other) && (subject == other) != (name == "eq?")) return false;
      } else if ((name == "any-of?" || name == "not-any-of?") && has) {
        bool in = false;
        for (uint32_t k = i + 2; k < j && !in; k++) {
          std::string_view v;
          in = text(st[k], v) && v == subject;
        }
        if (in != (name == "any-of?")) return false;
      }
      // (other predicates are directives: #set!, #is?; they do not filter)
    }
    i = j + 1;
  }
  return true;
}
}  // namespace

std::vector<CodeToken> nativeTokens(std::string_view lang, std::string_view body) {
  std::vector<CodeToken> out;
  const NativeGrammar* g = nativeGrammar(lang);
  if (!g) return out;
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, g->language());
  TSTree* tree = ts_parser_parse_string(parser, nullptr, body.data(), (uint32_t)body.size());
  uint32_t eo = 0;
  TSQueryError et;
  TSQuery* q = ts_query_new(g->language(), g->query.data(), (uint32_t)g->query.size(), &eo, &et);
  if (q) {
    struct Cap { u32 s, e, pat; int tag; };
    std::vector<Cap> caps;
    TSQueryCursor* cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, q, ts_tree_root_node(tree));
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m)) {
      if (!predicatesHold(q, m, body)) continue;
      for (u16 i = 0; i < m.capture_count; i++) {
        const TSQueryCapture& c = m.captures[i];
        uint32_t len = 0;
        const char* nm = ts_query_capture_name_for_id(q, c.index, &len);
        int tag = tokenTagFromCapture(std::string_view(nm, len));
        if (tag < 0) continue;
        caps.push_back({ts_node_start_byte(c.node), ts_node_end_byte(c.node), m.pattern_index, tag});
      }
    }
    ts_query_cursor_delete(cur);
    std::stable_sort(caps.begin(), caps.end(), [](const Cap& a, const Cap& b) {
      return a.s != b.s ? a.s < b.s : a.pat < b.pat;
    });
    u32 covered = 0;
    for (const Cap& c : caps) {
      if (c.s < covered || c.e <= c.s) continue;  // an earlier capture won
      out.push_back({c.s, c.e, (u8)c.tag});
      covered = c.e;
    }
    ts_query_delete(q);
  }
  ts_tree_delete(tree);
  ts_parser_delete(parser);
  return out;
}

}  // namespace tsr
