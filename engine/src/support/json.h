// A small JSON reader for host documents (settings, plan P1-03): objects,
// arrays, strings (with \u escapes and surrogate pairs), numbers, true,
// false, null. No exceptions; a malformed document returns an error message
// and its byte offset. Depth and size are bounded (hostile input is fuzzed:
// fuzz_settings).
#pragma once
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support.h"

namespace tsr {

struct JsonValue {
  enum class T : u8 { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JsonValue> arr;
  std::vector<std::string> keys;  // object members, insertion order, duplicates kept
  std::vector<JsonValue> vals;
  const JsonValue* get(std::string_view key) const {
    for (size_t k = keys.size(); k-- > 0;)
      if (keys[k] == key) return &vals[k];  // the last duplicate wins
    return nullptr;
  }
};

class JsonReader {
 public:
  static constexpr u32 kMaxDepth = 32;
  static constexpr size_t kMaxBytes = 1 << 20;

  // false on malformed input: error() and offset() say why and where
  bool parse(std::string_view text, JsonValue& out) {
    s_ = text;
    i_ = 0;
    err_ = nullptr;
    if (text.size() > kMaxBytes) return fail("document too large");
    ws();
    if (!value(out, 0)) return false;
    ws();
    if (i_ != s_.size()) return fail("trailing characters");
    return true;
  }
  const char* error() const { return err_; }
  size_t offset() const { return i_; }

 private:
  std::string_view s_;
  size_t i_ = 0;
  const char* err_ = nullptr;

  bool fail(const char* m) {
    if (!err_) err_ = m;
    return false;
  }
  void ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
  }
  bool lit(const char* w) {
    size_t n = std::strlen(w);
    if (s_.substr(i_, n) != w) return fail("bad literal");
    i_ += n;
    return true;
  }
  bool value(JsonValue& v, u32 depth) {
    if (depth > kMaxDepth) return fail("nested too deeply");
    if (i_ >= s_.size()) return fail("unexpected end");
    char c = s_[i_];
    if (c == '{') return object(v, depth);
    if (c == '[') return array(v, depth);
    if (c == '"') {
      v.t = JsonValue::T::Str;
      return string(v.str);
    }
    if (c == 't') { v.t = JsonValue::T::Bool; v.b = true; return lit("true"); }
    if (c == 'f') { v.t = JsonValue::T::Bool; v.b = false; return lit("false"); }
    if (c == 'n') { v.t = JsonValue::T::Null; return lit("null"); }
    if (c == '-' || (c >= '0' && c <= '9')) return number(v);
    return fail("unexpected character");
  }
  bool object(JsonValue& v, u32 depth) {
    v.t = JsonValue::T::Obj;
    i_++;
    ws();
    if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
    for (;;) {
      ws();
      if (i_ >= s_.size() || s_[i_] != '"') return fail("expected a key");
      std::string key;
      if (!string(key)) return false;
      ws();
      if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
      i_++;
      ws();
      JsonValue child;
      if (!value(child, depth + 1)) return false;
      v.keys.push_back(std::move(key));
      v.vals.push_back(std::move(child));
      ws();
      if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
      if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
      return fail("expected ',' or '}'");
    }
  }
  bool array(JsonValue& v, u32 depth) {
    v.t = JsonValue::T::Arr;
    i_++;
    ws();
    if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
    for (;;) {
      ws();
      JsonValue child;
      if (!value(child, depth + 1)) return false;
      v.arr.push_back(std::move(child));
      ws();
      if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
      if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
      return fail("expected ',' or ']'");
    }
  }
  static void utf8(std::string& out, u32 cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
      out += (char)(0xE0 | (cp >> 12));
      out += (char)(0x80 | ((cp >> 6) & 0x3F));
      out += (char)(0x80 | (cp & 0x3F));
    } else {
      out += (char)(0xF0 | (cp >> 18));
      out += (char)(0x80 | ((cp >> 12) & 0x3F));
      out += (char)(0x80 | ((cp >> 6) & 0x3F));
      out += (char)(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(u32& cp) {
    if (i_ + 4 > s_.size()) return fail("short \\u escape");
    cp = 0;
    for (int k = 0; k < 4; k++) {
      char h = s_[i_++];
      cp <<= 4;
      if (h >= '0' && h <= '9') cp |= (u32)(h - '0');
      else if (h >= 'a' && h <= 'f') cp |= (u32)(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') cp |= (u32)(h - 'A' + 10);
      else return fail("bad \\u escape");
    }
    return true;
  }
  bool string(std::string& out) {
    i_++;  // opening quote
    for (;;) {
      if (i_ >= s_.size()) return fail("unterminated string");
      unsigned char c = (unsigned char)s_[i_++];
      if (c == '"') return true;
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') { out += (char)c; continue; }
      if (i_ >= s_.size()) return fail("unterminated escape");
      char e = s_[i_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          u32 cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00) {  // a surrogate pair
            u32 lo;
            if (s_.substr(i_, 2) != "\\u") return fail("lone surrogate");
            i_ += 2;
            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("lone surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("lone surrogate");
          }
          utf8(out, cp);
          break;
        }
        default:
          return fail("bad escape");
      }
    }
  }
  bool number(JsonValue& v) {
    size_t st = i_;
    if (s_[i_] == '-') i_++;
    if (i_ >= s_.size()) return fail("bad number");
    if (s_[i_] == '0') i_++;
    else if (s_[i_] >= '1' && s_[i_] <= '9') while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') i_++;
    else return fail("bad number");
    if (i_ < s_.size() && s_[i_] == '.') {
      i_++;
      size_t d = i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') i_++;
      if (i_ == d) return fail("bad number");
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      i_++;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) i_++;
      size_t d = i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') i_++;
      if (i_ == d) return fail("bad number");
    }
    // the grammar above is strict JSON, so strtod (C locale) reads exactly it
    std::string tmp(s_.substr(st, i_ - st));
    v.t = JsonValue::T::Num;
    v.num = std::strtod(tmp.c_str(), nullptr);
    return true;
  }
};

// a JSON string literal for `s` (settings and products output)
inline void jsonString(std::string& out, std::string_view s) {
  out += '"';
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
  out += '"';
}

// writes a value back as JSON (numbers with up to 17 significant digits)
inline void jsonDump(std::string& out, const JsonValue& v) {
  switch (v.t) {
    case JsonValue::T::Null: out += "null"; return;
    case JsonValue::T::Bool: out += v.b ? "true" : "false"; return;
    case JsonValue::T::Num: appendf(out, "%.17g", v.num); return;
    case JsonValue::T::Str: jsonString(out, v.str); return;
    case JsonValue::T::Arr:
      out += '[';
      for (size_t k = 0; k < v.arr.size(); k++) {
        if (k) out += ',';
        jsonDump(out, v.arr[k]);
      }
      out += ']';
      return;
    case JsonValue::T::Obj:
      out += '{';
      for (size_t k = 0; k < v.keys.size(); k++) {
        if (k) out += ',';
        jsonString(out, v.keys[k]);
        out += ':';
        jsonDump(out, v.vals[k]);
      }
      out += '}';
      return;
  }
}

}  // namespace tsr
