// Knuth–Plass line breaking with TeX item semantics (plans P0-12, P1-14;
// the rules are at the top of break.cc, the item projection in items.h). su
// inputs, bounded double costs, an exact active-list search.
#pragma once
#include <list>
#include <unordered_map>

#include "../emit/emit.h"
#include "items.h"

namespace tsr {

struct BItem;

struct BreakResult {
  // per line, the HList item the next line starts at (its first Box or Disc
  // after the break's discard; the last: the item count), ascending
  std::vector<u32> breakpoints;
  double cost = 0;                 // demerits
  std::vector<u32> overfullLines;  // rescued lines (final pass): content wider than the line
  bool feasible = true;            // false: the final pass had to rescue
  u8 pass = 3;                     // the pass that found it (1 tolerance, 2 emergency, 3 final)
};

// A paragraph's shape (plan P3-08; design T6 ParShape, TeX's \parshape):
// line i's slot in its container's content box — its start offset and its
// width, `rest` after the explicit ones (floats on either side, stacked
// floats of any widths, a float starting mid-paragraph, a hanging indent).
// The breaker reads the widths, layout the offsets; the slot width is the
// one definition of the measure.
struct LineSlot {
  Su left = 0, width = 0;
};
struct ParShape {
  std::vector<LineSlot> lines;
  LineSlot rest;
  ParShape() = default;
  explicit ParShape(Su width) : rest{0, width} {}
  const LineSlot& at(u32 i) const { return i < lines.size() ? lines[i] : rest; }
};

// What the search reads besides the items (design T6 S2), defaults = today:
// no tolerance pass and no emergency pass, only the final pass with TeX's
// rescue. Pass 1 accepts only lines of cost <= tolerance; pass 2 adds
// emergencyStretch to every line's stretch.
constexpr double kNoTolerance = -1;
// (plan P3-09; design T6 LineEnds, D-Y01) the glue at a line's ends, which
// the breaker optimizes with and layout realizes: TeX's \leftskip /
// \rightskip and, for a line ended by a Forced break (the paragraph end, a
// hard line break), \parfillskip. order 1 is fil (it absorbs any slack);
// rigidInterior: the interior glue keeps its shrink but stretches nothing
// (ragged, centred and flush lines). One preset per alignment.
struct EndGlue {
  Su w = 0, stretch = 0;
  u8 order = 0;
};
struct LineEnds {  // (default: justified — the last line fil)
  EndGlue start, end;                  // a line ended by an optional break
  EndGlue lastStart, lastEnd{0, 0, 1};  // a line ended by a Forced break
  bool rigidInterior = false;
  enum class Preset : u8 { Justify, Left, Center, Right };
  // em: the ragged lines' finite end stretch unit (D-Y01: 2em at the free
  // end of a flush line, 1em each side of a centred one)
  static LineEnds preset(Preset p, Su em) {
    LineEnds e;
    const EndGlue fil{0, 0, 1};
    switch (p) {
      case Preset::Justify: e.lastEnd = fil; break;
      case Preset::Left: e = {{}, {0, 2 * em, 0}, {}, fil, true}; break;
      case Preset::Center: e = {{0, em, 0}, {0, em, 0}, fil, fil, true}; break;
      case Preset::Right: e = {{0, 2 * em, 0}, {}, fil, {}, true}; break;
    }
    return e;
  }
};
struct BreakParams {
  CostParams cost;
  double tolerance = kNoTolerance;
  Su emergencyStretch = 0;
  LineEnds ends;  // the stream's (layout sets it per stream)
};

// items: the breaker's view of an HList (items.h hlistToItems), nItems the
// HList's item count
BreakResult breakItems(const std::vector<BItem>& items, u32 nItems, const ParShape& shape,
                       const BreakParams& params);

// The cached form (editor-design.md §2): KP reads only the items, the line
// widths and the params, so a result is keyed by exactly those — the
// complete input's bytes, compared on a hit — and kept in the Session's
// memo slot (plan P1-21): an editing session re-breaks only the paragraphs
// a keystroke changed. No memo: break uncached.
class BreakMemo {
 public:
  static constexpr size_t kBudgetBytes = size_t(16) << 20;
  const BreakResult* find(u64 hash, std::string_view key);
  void put(u64 hash, std::string_view key, const BreakResult& r);
  void setBudget(size_t bytes) { budget_ = bytes ? bytes : kBudgetBytes; }
  size_t bytes() const { return bytes_; }

 private:
  struct Entry {
    std::string key;
    BreakResult result;
    std::list<u64>::iterator lru;
  };
  static size_t cost(const Entry& e);
  void erase(std::unordered_map<u64, Entry>::iterator it);
  std::unordered_map<u64, Entry> map_;
  std::list<u64> lru_;
  size_t bytes_ = 0;
  size_t budget_ = kBudgetBytes;
};
// (plan P4-08) an HList broken: its items read as the breaker's (no memo:
// uncached)
BreakResult breakLinesCached(const HList& h, const ParShape& shape, const BreakParams& params, BreakMemo* memo);

}  // namespace tsr
