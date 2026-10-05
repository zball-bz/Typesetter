// The inline object registry (plan P1-13; design T5 "InlineObject
// protocol"): what each object kind is at its edges and how its parts are
// measured and painted. The shaper's flatten table (the `inline` column of
// engine/schema/schema.json, generated into KindInfo::inl) sends a node of
// row `object` here; a kind that cannot appear inline becomes an Error
// object with a `shape-unsupported` diagnostic.
//
// Two phases: expand (in emit) writes the structure — Box parts, ObjectSpace
// glue and penalties between them — without metrics, or one placeholder part
// when the structure itself needs metrics (a formula with unmeasured
// text-font runs, until T8 splits segmentation from widths); resolveWidths
// then lays the formula out for that list alone and splices its parts in.
// Extents are per part; layout and paint ask the part, never the kind.
#pragma once
#include "hlist.h"
#include "textrules.h"

namespace tsr {

struct ObjectKind {
  const char* name;
  CC firstCC, lastCC;  // default edge classes: formulas are Latin-class (App C);
                       //   images and raw markup sit like an ideograph
  bool copiesSource;   // copy reads its source (data-syn="math"); else synthetic
};

const ObjectKind& objectKind(ObjKind k);
// the object an inline node becomes (flatten row `object`)
ObjKind objectKindOf(Kind k);

// a Box part's extents above / below the baseline — the line-height shim of
// plan P1-13 over the three copies layout had (P1-17's materializeLines
// takes it over)
inline const ObjPart* objectPart(const HList& h, const HItem& it) {
  if (it.k != IK::Box || h.runs[it.run].rc != RealizeClass::Object) return nullptr;
  return &h.parts[h.specs[it.aux].obj];
}

// tsrc --stage=hlist: the object table
void dumpObjects(std::string& out, const HList& h, const Interner& strs, const char* indent);

}  // namespace tsr
