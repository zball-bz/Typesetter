// Foundations: fixed-point su units, arena, string interner, diagnostics, utf8.
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>
#include <type_traits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tsr {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i32 = int32_t;
using i64 = int64_t;

// --- su: 1/64 CSS px fixed point (document-model §6.1) ---
using Su = i32;
inline Su suCeilPx(double px) { return (Su)std::ceil(px * 64.0 - 1e-9); }
inline Su suFloorPx(double px) { return (Su)std::floor(px * 64.0 + 1e-9); }
inline Su suRoundPx(double px) { return (Su)std::llround(px * 64.0); }
inline double suToPx(Su s) { return (double)s / 64.0; }

struct Span {
  u32 start = 0, end = 0;
  bool empty() const { return end <= start; }
};

// --- arena: document-scoped bump allocator ---
// Node types placement-new'd here carry std::vector members whose heap
// buffers are NOT arena memory — construct them through make<T>() so their
// destructors run at arena teardown (LeakSanitizer-clean by construction).
class Arena {
 public:
  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  ~Arena() {
    for (size_t i = fins_.size(); i > 0; i--) fins_[i - 1].fn(fins_[i - 1].p);
    for (char* b : blocks_) std::free(b);
  }

  template <class T>
  T* make() {
    T* p = new (alloc(sizeof(T), alignof(T))) T();
    if constexpr (!std::is_trivially_destructible_v<T>)
      fins_.push_back({p, [](void* q) { ((T*)q)->~T(); }});
    return p;
  }
  void* alloc(size_t n, size_t align = 8) {
    uintptr_t base = (uintptr_t)cur_;
    uintptr_t p = (base + pos_ + align - 1) & ~(uintptr_t)(align - 1);
    if (!cur_ || p + n > base + cap_) {
      grow(n + align);
      base = (uintptr_t)cur_;
      p = (base + align - 1) & ~(uintptr_t)(align - 1);
    }
    pos_ = (p + n) - base;
    return (void*)p;
  }
  template <class T>
  T* allocArray(size_t n) {
    return (T*)alloc(n * sizeof(T), alignof(T));
  }
  char* copy(std::string_view s) {
    char* p = (char*)alloc(s.size() + 1, 1);
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = 0;
    return p;
  }

 private:
  void grow(size_t min) {
    size_t sz = min > 64 * 1024 ? min : 64 * 1024;
    cur_ = (char*)std::malloc(sz);
    cap_ = sz;
    pos_ = 0;
    blocks_.push_back(cur_);
  }
  struct Fin {
    void* p;
    void (*fn)(void*);
  };
  std::vector<Fin> fins_;
  std::vector<char*> blocks_;
  char* cur_ = nullptr;
  size_t cap_ = 0, pos_ = 0;
};

// --- interner ---
using StrRef = u32;
class Interner {
 public:
  // ref 0 is pre-interned "" so StrRef 0 is a safe "unset" sentinel
  // everywhere (marker, linkUrl, labels, …).
  explicit Interner(Arena& a) : arena_(a) { intern(""); }
  StrRef intern(std::string_view s) {
    auto it = map_.find(s);
    if (it != map_.end()) return it->second;
    char* p = arena_.copy(s);
    std::string_view v(p, s.size());
    StrRef r = (StrRef)strs_.size();
    strs_.push_back(v);
    map_.emplace(v, r);
    return r;
  }
  std::string_view get(StrRef r) const { return strs_[r]; }
  size_t count() const { return strs_.size(); }

 private:
  Arena& arena_;
  std::vector<std::string_view> strs_;
  std::unordered_map<std::string_view, StrRef> map_;
};

// --- diagnostics (document-model §10) ---
enum class Sev : u8 { Error, Warning, Info };
struct Diag {
  Sev sev;
  const char* code;
  Span span;
  std::string msg;
};
struct DiagSink {
  std::vector<Diag> items;
  void add(Sev s, const char* code, Span sp, std::string msg) {
    items.push_back({s, code, sp, std::move(msg)});
  }
};

// --- utf8 ---
// Decodes the codepoint at i, advances i. Invalid bytes decode as themselves.
inline u32 utf8Next(std::string_view s, u32& i) {
  u8 c = (u8)s[i++];
  if (c < 0x80) return c;
  u32 cp = 0;
  int extra = 0;
  if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
  else return c;
  for (int k = 0; k < extra; k++) {
    if (i >= s.size() || ((u8)s[i] & 0xC0) != 0x80) return c;
    cp = (cp << 6) | ((u8)s[i++] & 0x3F);
  }
  return cp;
}

inline bool isCjk(u32 cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
         (cp >= 0x20000 && cp <= 0x2FA1F);
}

// clreq punctuation classes (SC horizontal; v2 App C).
inline bool isPunctOpen(u32 cp) {
  switch (cp) {
    case 0xFF08: case 0x3014: case 0xFF3B: case 0xFF5B:  // （〔［｛
    case 0x300A: case 0x3008: case 0x300C: case 0x300E:  // 《〈「『
    case 0x3010: case 0x201C: case 0x2018:               // 【“‘
      return true;
    default:
      return false;
  }
}
inline bool isPunctClose(u32 cp) {
  switch (cp) {
    case 0xFF09: case 0x3015: case 0xFF3D: case 0xFF5D:  // ）〕］｝
    case 0x300B: case 0x3009: case 0x300D: case 0x300F:  // 》〉」』
    case 0x3011: case 0x201D: case 0x2019:               // 】”’
    case 0x3002: case 0xFF0E: case 0xFF0C: case 0x3001:  // 。．，、
    case 0xFF1B: case 0xFF1A: case 0xFF01: case 0xFF1F:  // ；：！？
      return true;
    default:
      return false;
  }
}
// Ideograph/kana — CJK glyphs that set solid at 1em with stretchable glue.
inline bool isCjkIdeo(u32 cp) {
  return isCjk(cp) && !isPunctOpen(cp) && !isPunctClose(cp);
}

// Decodes the codepoint ENDING at byte offset `end` (exclusive).
inline u32 utf8PrevCp(std::string_view s, u32 end) {
  u32 i = end;
  while (i > 0 && ((u8)s[i - 1] & 0xC0) == 0x80) i--;
  if (i > 0) i--;
  u32 j = i;
  return utf8Next(s, j);
}

// --- dump helper ---
inline void appendf(std::string& out, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n > 0) out.append(buf, (size_t)((n < (int)sizeof buf) ? n : (int)sizeof buf - 1));
}

// Image src allow-list (figure-design.md §5): relative paths, http(s), and
// data:image/* — never javascript: or other schemes. Shared by both
// serializers and emit.
inline bool safeImageSrc(std::string_view src) {
  if (src.empty()) return false;
  size_t colon = src.find(':');
  if (colon == std::string_view::npos) return true;  // relative / absolute path
  // a ':' after the first '/', '?' or '#' is not a scheme separator
  size_t stop = src.find_first_of("/?#");
  if (stop != std::string_view::npos && stop < colon) return true;
  std::string_view scheme = src.substr(0, colon);
  std::string low(scheme);
  for (char& c : low) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
  if (low == "http" || low == "https") return true;
  if (low == "data") return src.substr(colon + 1).rfind("image/", 0) == 0;
  return false;
}

// JSON-style escaping for dumps and JS string literals.
inline void appendEscaped(std::string& out, std::string_view s) {
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) appendf(out, "\\u%04x", c);
        else out += (char)c;
    }
  }
}

}  // namespace tsr
