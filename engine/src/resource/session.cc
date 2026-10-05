#include "session.h"

#include <cstring>

#include "../support/json.h"
#include "../syntax/exports.h"

namespace tsr {

const std::vector<Answerer>& answerers() {
  static const std::vector<Answerer> rows = {
      // the engine's own language (plan P1-09; code-design §2)
      {ResKind::codeTokens, "codeTokens.tsm",
       [](std::string_view lang, std::string_view body, std::vector<CodeToken>& out) {
         if (lang != "tsm") return false;
         out = syntaxTokens(body);
         return true;
       }},
  };
  return rows;
}

Session::Session(size_t budgetBytes) : enabled(answerers().size(), true), budget_(budgetBytes) {
  breakMemo.setBudget(budgetBytes / 4);
}

bool Session::configure(std::string_view json) {
  JsonValue v;
  JsonReader rd;
  if (!rd.parse(json, v) || v.t != JsonValue::T::Obj) return false;
  if (const JsonValue* b = v.get("budgetBytes"); b && b->t == JsonValue::T::Num && b->num > 0) {
    budget_ = (size_t)b->num;
    breakMemo.setBudget(budget_ / 4);
  }
  if (const JsonValue* a = v.get("answerers"); a && a->t == JsonValue::T::Obj)
    for (size_t i = 0; i < answerers().size(); i++)
      if (const JsonValue* on = a->get(answerers()[i].name); on && on->t == JsonValue::T::Bool) enabled[i] = on->b;
  return true;
}

u32 Session::metricKey(std::string_view canonical) {
  auto it = mkIds_.find(std::string(canonical));
  if (it != mkIds_.end()) return it->second;
  const u32 id = (u32)mkIds_.size();
  mkIds_.emplace(std::string(canonical), id);
  return id;
}

bool Session::width(u32 mk, std::string_view text, double& px) {
  probe_.assign((const char*)&mk, 4);
  probe_.append(text);
  auto it = widths_[0].find(std::string_view(probe_));
  if (it != widths_[0].end()) {
    px = it->second;
    stats.widthHits++;
    return true;
  }
  it = widths_[1].find(std::string_view(probe_));
  if (it == widths_[1].end()) {
    stats.widthMisses++;
    return false;
  }
  px = it->second;
  stats.widthHits++;
  putWidth(mk, text, px);  // promote
  return true;
}

void Session::putWidth(u32 mk, std::string_view text, double px) {
  probe_.assign((const char*)&mk, 4);
  probe_.append(text);
  auto [it, fresh] = widths_[0].try_emplace(probe_, px);
  if (!fresh) {
    it->second = px;
    return;
  }
  widthBytes_ += probe_.size() + 48;
  if (widthBytes_ > budget_ / 2) {  // the oldest generation goes
    widths_[1] = std::move(widths_[0]);
    widths_[0] = Map{};
    widthBytes_ = 0;
  }
}

bool Session::vmet(u32 mk, double& asc, double& desc) const {
  auto it = vmets_.find(mk);
  if (it == vmets_.end()) return false;
  asc = it->second.first;
  desc = it->second.second;
  return true;
}
void Session::putVmet(u32 mk, double asc, double desc) { vmets_[mk] = {asc, desc}; }

bool Session::tokens(std::string_view lang, std::string_view body, std::vector<CodeToken>& out) {
  std::string k(lang);
  k += '\0';
  k.append(body);
  auto it = tokens_.find(k);
  if (it == tokens_.end()) return false;
  out = it->second;
  return true;
}
void Session::putTokens(std::string_view lang, std::string_view body, const std::vector<CodeToken>& toks) {
  if (tokens_.size() > 4096) tokens_.clear();  // bounded: eviction only costs a re-request
  std::string k(lang);
  k += '\0';
  k.append(body);
  tokens_[k] = toks;
}

bool Session::answerTokens(std::string_view lang, std::string_view body, std::vector<CodeToken>& out) const {
  for (size_t i = 0; i < answerers().size(); i++)
    if (enabled[i] && answerers()[i].kind == ResKind::codeTokens && answerers()[i].answer(lang, body, out)) return true;
  return false;
}

}  // namespace tsr
