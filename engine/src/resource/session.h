// The Session (plan P1-21; design T9 A5): the host-owned, content-keyed
// answer cache shared by the documents of one host — one per worker, one
// per Node process, a fresh one per golden fixture. Keys are complete
// (resource/resources.def: the full metric key, the code body), so an entry
// can only be unused, never wrong: there are no generations and no
// invalidation call. A document looks a need up in this order: its own
// table, an in-engine answerer, the Session (copied in), the host. Answers
// the host marks `store` are written through. It also holds the memo slots
// (the KP memo).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../break/break.h"
#include "../code/tokens.h"
#include "resources.gen.h"

namespace tsr {

// An in-engine answerer (design T9 A5): a kind and a key family the engine
// answers itself — deterministic, so never written to the cache. A host that
// wants its own disables it when it creates the Session.
struct Answerer {
  ResKind kind;
  const char* name;  // "codeTokens.tsm"
  bool (*answer)(std::string_view lang, std::string_view body, std::vector<CodeToken>& out);
};
const std::vector<Answerer>& answerers();

class Session {
 public:
  explicit Session(size_t budgetBytes = 64u << 20);
  // {"budgetBytes": n, "answerers": {"codeTokens.tsm": false}}: unknown keys
  // are ignored; false = malformed
  bool configure(std::string_view json);
  // a metric key's canonical bytes (Doc::canonicalKey) → this session's id
  u32 metricKey(std::string_view canonical);
  bool width(u32 mk, std::string_view text, double& px);
  void putWidth(u32 mk, std::string_view text, double px);
  bool vmet(u32 mk, double& asc, double& desc) const;
  void putVmet(u32 mk, double asc, double desc);
  bool tokens(std::string_view lang, std::string_view body, std::vector<CodeToken>& out);
  void putTokens(std::string_view lang, std::string_view body, const std::vector<CodeToken>& toks);
  // in-engine answerers (design T9 A5): deterministic and recomputable, so
  // never written to the cache — codeTokens 'tsm' (the engine's own
  // language: syntax/exports.h)
  bool answerTokens(std::string_view lang, std::string_view body, std::vector<CodeToken>& out) const;
  std::vector<bool> enabled;  // per answerers() row

  BreakMemo breakMemo;  // the KP memo slot
  int refs = 0;         // documents attached
  struct Stats {
    u64 widthHits = 0, widthMisses = 0;
  } stats;

 private:
  struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
  };
  using Map = std::unordered_map<std::string, double, SvHash, std::equal_to<>>;
  // the width store: two generations — the current one fills up to half the
  // budget, then becomes the previous one (the older entries go); a hit in
  // the previous generation moves to the current one (an LRU approximation)
  Map widths_[2];
  size_t widthBytes_ = 0;
  std::string probe_;  // the lookup key: mk bytes + text
  std::unordered_map<std::string, u32> mkIds_;
  std::unordered_map<u32, std::pair<double, double>> vmets_;
  std::unordered_map<std::string, std::vector<CodeToken>> tokens_;
  size_t budget_;
};

}  // namespace tsr
