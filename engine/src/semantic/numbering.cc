#include "numbering.h"

namespace tsr {

namespace {

// a counting symbol at p: its length (and the system's name for {name}), 0 = none
size_t symbolAt(std::string_view p, size_t i, std::string& name) {
  static constexpr std::string_view kSymbols[] = {"1", "a", "A", "i", "I", "\xE2\x91\xA0" /* ① */,
                                                  "\xE4\xB8\x80" /* 一 */, "*"};
  if (p[i] == '{') {
    size_t e = p.find('}', i);
    if (e != std::string_view::npos && e > i + 1) {
      name = std::string(p.substr(i + 1, e - i - 1));
      return e + 1 - i;
    }
    return 0;
  }
  for (std::string_view s : kSymbols)
    if (p.substr(i, s.size()) == s) {
      name = std::string(s);
      return s.size();
    }
  return 0;
}

struct Pattern {
  std::string prefix, suffix;
  std::vector<std::string> syms, seps;
};

Pattern parse(std::string_view p) {
  Pattern out;
  std::string lit;
  bool any = false;
  for (size_t i = 0; i < p.size();) {
    std::string name;
    if (size_t n = symbolAt(p, i, name)) {
      if (any) out.seps.push_back(lit);
      else out.prefix = lit;
      lit.clear();
      out.syms.push_back(name);
      any = true;
      i += n;
    } else {
      lit += p[i++];
    }
  }
  if (!any) {  // no symbol: decimal, with the text as a prefix
    out.prefix = lit;
    out.syms.push_back("1");
  } else {
    out.suffix = lit;
  }
  return out;
}

std::string roman(int n, bool upper) {
  if (n <= 0 || n >= 4000) return std::to_string(n);
  static const struct { int v; const char* s; } kR[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
                                                        {90, "xc"},  {50, "l"},   {40, "xl"},  {10, "x"},   {9, "ix"},
                                                        {5, "v"},    {4, "iv"},   {1, "i"}};
  std::string out;
  for (const auto& r : kR)
    while (n >= r.v) {
      out += r.s;
      n -= r.v;
    }
  if (upper)
    for (char& c : out) c = (char)(c - 'a' + 'A');
  return out;
}

// bijective base-k (a, b, …, z, aa, …)
std::string alpha(int n, const std::vector<std::string>& syms) {
  if (n <= 0 || syms.empty()) return std::to_string(n);
  std::string out;
  const int k = (int)syms.size();
  while (n > 0) {
    n--;
    out = syms[n % k] + out;
    n /= k;
  }
  return out;
}

std::string chinese(int n) {
  static const char* kD[] = {"\xE9\x9B\xB6", "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89", "\xE5\x9B\x9B",
                             "\xE4\xBA\x94", "\xE5\x85\xAD", "\xE4\xB8\x83", "\xE5\x85\xAB", "\xE4\xB9\x9D"};
  static const char* kTen = "\xE5\x8D\x81";  // 十
  if (n < 0 || n >= 100) return std::to_string(n);
  if (n < 10) return kD[n];
  std::string out;
  if (n >= 20) out += kD[n / 10];
  out += kTen;
  if (n % 10) out += kD[n % 10];
  return out;
}

std::string component(const std::string& sym, int n, const Registry& reg) {
  if (sym == "1") return std::to_string(n);
  if (sym == "a" || sym == "A") {
    std::vector<std::string> letters;
    for (char c = 'a'; c <= 'z'; c++) letters.push_back(std::string(1, sym == "a" ? c : (char)(c - 'a' + 'A')));
    return alpha(n, letters);
  }
  if (sym == "i" || sym == "I") return roman(n, sym == "I");
  if (sym == "\xE2\x91\xA0") {  // ①…⑳ ㉑…㉟ ㊱…㊿, then arabic
    const u32 cp = n >= 1 && n <= 20 ? 0x2460 + (u32)(n - 1) : n >= 21 && n <= 35 ? 0x3251 + (u32)(n - 21)
                   : n >= 36 && n <= 50 ? 0x32B1 + (u32)(n - 36) : 0;
    if (!cp) return std::to_string(n);
    std::string out;
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
    return out;
  }
  if (sym == "\xE4\xB8\x80") return chinese(n);
  if (sym == "*") {  // *, †, ‡, §, ¶, ‖, then doubled
    static const char* kS[] = {"*", "\xE2\x80\xA0", "\xE2\x80\xA1", "\xC2\xA7", "\xC2\xB6", "\xE2\x80\x96"};
    if (n <= 0) return std::to_string(n);
    std::string s = kS[(n - 1) % 6], out;
    for (int k = 0; k <= (n - 1) / 6; k++) out += s;
    return out;
  }
  for (const CounterSystem& s : reg.systems) {
    if (s.name != sym) continue;
    const int k = (int)s.symbols.size();
    switch (s.mode) {
      case CounterSystem::Mode::Alphabetic: return alpha(n, s.symbols);
      case CounterSystem::Mode::Cyclic: return n > 0 ? s.symbols[(n - 1) % k] : std::to_string(n);
      case CounterSystem::Mode::Fixed: return n >= 1 && n <= k ? s.symbols[n - 1] : std::to_string(n);
      case CounterSystem::Mode::Numeric: {
        if (n == 0) return s.symbols[0];
        if (k < 2) return std::to_string(n);
        std::string out;
        for (int m = n; m > 0; m /= k) out = s.symbols[m % k] + out;
        return out;
      }
    }
  }
  return std::to_string(n);
}

}  // namespace

std::string formatNumber(std::string_view pattern, const std::vector<int>& comps, const Registry& reg) {
  if (pattern.empty()) {  // 1.1: decimal components joined by '.'
    std::string out;
    for (size_t i = 0; i < comps.size(); i++) {
      if (i) out += '.';
      out += std::to_string(comps[i]);
    }
    return out;
  }
  const Pattern p = parse(pattern);
  std::string out = p.prefix;
  for (size_t i = 0; i < comps.size(); i++) {
    if (i) out += p.seps.empty() ? "." : p.seps[std::min(i - 1, p.seps.size() - 1)];
    out += component(p.syms[std::min(i, p.syms.size() - 1)], comps[i], reg);
  }
  return out + p.suffix;
}

Counters::Counters(const Registry& reg)
    : reg_(reg), v_(reg.counters.size()), pattern_(reg.counters.size()), sup_(reg.counters.size()),
      keyed_(reg.counters.size()) {
  for (size_t c = 0; c < reg.counters.size(); c++) {
    v_[c] = reg.counters[c].start;
    pattern_[c] = reg.counters[c].pattern;
  }
}

int Counters::levelOf(u16 c, const ContentNode* n) const {
  const CounterDef& d = reg_.counters[c];
  if (!d.byLevel) return 1;
  int level = attrInt(n, d.levelArg, 1);
  return level < 1 ? 1 : level > d.depth ? d.depth : level;
}

// counters within c restart when c steps at a level they are numbered under
void Counters::restartWithin(u16 c, int level) {
  for (size_t d = 0; d < reg_.counters.size(); d++)
    if (reg_.counters[d].within == c && reg_.counters[d].withinDepth >= level) {
      v_[d] = reg_.counters[d].start;
      restartWithin((u16)d, 1);
    }
}

std::string Counters::step(u16 c, int level) {
  const CounterDef& d = reg_.counters[c];
  std::vector<int>& v = v_[c];
  size_t depth = d.byLevel ? (size_t)level : 1;
  // a skipped level counts 0 (1.0.1) or 1 (1.1.1); the new level starts at 0
  if (v.size() < depth) {
    v.resize(depth - 1, d.gapOne ? 1 : 0);
    v.push_back(0);
  }
  v.resize(depth);
  v[depth - 1]++;
  restartWithin(c, (int)depth);
  return format(c);
}

// a counter's components with its parents' first (withinDepth) components
// before them, each in its own current pattern
std::string Counters::formatComps(u16 c, const std::vector<int>& comps) const {
  const CounterDef& d = reg_.counters[c];
  std::string out;
  if (d.within != kNoIndex) {
    std::vector<int> pv = v_[d.within];
    pv.resize((size_t)d.withinDepth, 0);
    out = formatComps(d.within, pv) + d.withinSep;
  }
  return out + formatNumber(pattern_[c], comps, reg_);
}

std::string Counters::format(u16 c) const { return formatComps(c, v_[c]); }

void Counters::apply(u16 c, const Event& ev) {
  std::vector<int>& v = v_[c];
  if (!ev.set.empty()) {
    v.clear();
    int cur = 0;
    bool digit = false, neg = false;
    for (char ch : ev.set) {
      if (ch == '-' && !digit) neg = true;
      else if (ch >= '0' && ch <= '9') {
        cur = cur * 10 + (ch - '0');
        digit = true;
      } else if (digit || neg) {
        v.push_back(neg ? -cur : cur);
        cur = 0;
        digit = neg = false;
      }
    }
    if (digit) v.push_back(neg ? -cur : cur);
    restartWithin(c, 1);
  }
  if (ev.step > 0) step(c, ev.step);
  if (ev.add) {
    if (v.empty()) v.push_back(0);
    v.back() += ev.add;
  }
  if (!ev.pattern.empty()) pattern_[c] = ev.pattern;
  if (ev.supplement.set()) sup_[c] = ev.supplement;
}

int Counters::keyed(u16 c, const std::string& key) {
  Keyed& k = keyed_[c];
  auto it = k.ord.find(key);
  if (it != k.ord.end()) return it->second;
  int n = (int)k.order.size() + 1;
  k.ord.emplace(key, n);
  k.order.push_back(key);
  return n;
}

int Counters::keyedIfSeen(u16 c, const std::string& key) const {
  auto it = keyed_[c].ord.find(key);
  return it == keyed_[c].ord.end() ? 0 : it->second;
}

u16 Counters::counterNamed(std::string_view name) const {
  for (size_t k = 0; k < reg_.counters.size(); k++)
    if (reg_.counters[k].name == name) return (u16)k;
  return kNoIndex;
}

}  // namespace tsr
