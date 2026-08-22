#include "hyphen.h"

#include "../../gen/hyphen_en_us.h"

namespace tsr {

using namespace hyphen_en_us;

static i32 childOf(u32 node, char c) {
  for (u32 e = kNodeEdgeStart[node]; e < kNodeEdgeStart[node + 1]; e++)
    if ((char)kEdgeChar[e] == c) return (i32)kEdgeChild[e];
  return -1;
}

std::vector<u32> hyphenPoints(std::string_view word) {
  std::vector<u32> out;
  const u32 len = (u32)word.size();
  if (len < 5) return out;

  std::string lower;
  lower.reserve(len + 2);
  lower += '.';
  for (char c : word) {
    if (c >= 'A' && c <= 'Z') lower += (char)(c - 'A' + 'a');
    else if (c >= 'a' && c <= 'z') lower += c;
    else return out;  // ASCII letters only (en-US)
  }
  lower += '.';

  // exceptions (binary search over sorted table)
  {
    std::string_view w(lower.data() + 1, len);
    int lo = 0, hi = kExcCount - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      int cmp = w.compare(kExcWords[mid]);
      if (cmp == 0) {
        for (u32 k = kExcMarkerOff[mid]; k < kExcMarkerOff[mid + 1]; k++)
          out.push_back(kExcMarkerData[k]);
        return out;
      }
      if (cmp < 0) hi = mid - 1;
      else lo = mid + 1;
    }
  }

  std::vector<u8> levels(len + 1, 0);
  const u32 L = (u32)lower.size();
  for (u32 K = 0; K + 3 <= L; K++) {
    const u32 pe = (K <= 1) ? 0 : K - 1;  // slice 0 and 1 share entity index 0
    i32 node = 0;
    for (u32 i = K; i < L; i++) {
      node = childOf((u32)node, lower[i]);
      if (node < 0) break;
      i32 pat = kNodePattern[node];
      if (pat >= 0) {
        for (u32 k = kLevelOff[pat]; k < kLevelOff[pat + 1]; k++) {
          u32 idx = pe + (k - kLevelOff[pat]);
          if (idx < levels.size() && kLevelData[k] > levels[idx]) levels[idx] = kLevelData[k];
        }
      }
    }
  }
  levels[0] = levels[1] = 0;
  if (levels.size() >= 2) {
    levels[levels.size() - 1] = 0;
    levels[levels.size() - 2] = 0;
  }
  for (u32 i = 0; i < levels.size(); i++)
    if (levels[i] & 1) out.push_back(i);
  return out;
}

}  // namespace tsr
