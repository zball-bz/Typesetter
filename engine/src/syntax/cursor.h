// A leaf's inline text as the surface lexer reads it (plan P1-06; design T1
// SurfaceLexer, SpanCursor). The leaf's line slices are joined by exactly one
// '\n': a line join is structural — CRLF terminators, trailing blanks and
// container prefixes between the lines never reach the lexer — and every scan
// runs over this view, so none can leave its leaf. Offsets in the view map
// back to raw source offsets for spans.
#pragma once
#include <algorithm>

#include "../source/source.h"

namespace tsr {

class LeafText {
 public:
  LeafText(std::string_view all, const std::vector<Span>& lines) {
    if (lines.empty()) return;
    base_ = lines.front().start;
    // the common case: consecutive lines separated by a bare '\n' — the view
    // is the raw source itself
    bool adjacent = true;
    for (size_t k = 0; k + 1 < lines.size() && adjacent; k++)
      adjacent = lines[k + 1].start == lines[k].end + 1 && all[lines[k].end] == '\n';
    if (adjacent) {
      t_ = all.substr(base_, lines.back().end - base_);
      return;
    }
    for (size_t k = 0; k < lines.size(); k++) {
      if (k) own_ += '\n';
      starts_.push_back((u32)own_.size());
      raws_.push_back(lines[k].start);
      own_ += all.substr(lines[k].start, lines[k].end - lines[k].start);
    }
    t_ = own_;
  }
  LeafText(const LeafText&) = delete;
  LeafText& operator=(const LeafText&) = delete;

  std::string_view text() const { return t_; }
  u32 size() const { return (u32)t_.size(); }
  // raw source offset of view offset c (a join maps to its line's end; the
  // view's end to the last line's end)
  u32 raw(u32 c) const {
    if (starts_.empty()) return base_ + c;
    size_t k = (size_t)(std::upper_bound(starts_.begin(), starts_.end(), c) - starts_.begin()) - 1;
    return raws_[k] + (c - starts_[k]);
  }
  Span rawSpan(u32 a, u32 b) const { return {raw(a), raw(b)}; }
  // view offset of raw offset r (r inside one of the lines)
  u32 view(u32 r) const {
    if (starts_.empty()) return r - base_;
    size_t k = (size_t)(std::upper_bound(raws_.begin(), raws_.end(), r) - raws_.begin()) - 1;
    return starts_[k] + (r - raws_[k]);
  }

 private:
  std::string_view t_;
  std::string own_;               // backing store when the lines are not adjacent
  std::vector<u32> starts_, raws_;  // per line: view offset, raw offset
  u32 base_ = 0;
};

}  // namespace tsr
