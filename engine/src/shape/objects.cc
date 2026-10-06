#include "objects.h"

namespace tsr {

namespace {

const ObjectKind kKindsTable[] = {
    {"math", CC::Alpha, CC::Alpha, true},
    {"image", CC::Ideo, CC::Ideo, false},
    {"raw", CC::Ideo, CC::Ideo, false},
    {"error", CC::Alpha, CC::Alpha, true},
};

}  // namespace

const ObjectKind& objectKind(ObjKind k) { return kKindsTable[(u8)k]; }

ObjKind objectKindOf(Kind k) {
  switch (k) {
    case Kind::mathinline: return ObjKind::Math;
    case Kind::image: return ObjKind::Image;
    case Kind::raw: return ObjKind::Raw;
    case Kind::doc: case Kind::para: case Kind::heading: case Kind::list: case Kind::item: case Kind::quote:
    case Kind::codeblock: case Kind::rule: case Kind::group: case Kind::table: case Kind::trow:
    case Kind::tcell: case Kind::term: case Kind::collect: case Kind::mathblock: case Kind::error:
    case Kind::comment: case Kind::text: case Kind::styled: case Kind::link: case Kind::code: case Kind::ref:
    case Kind::hardbreak: case Kind::seq: case Kind::note: case Kind::field: case Kind::event:
    case Kind::entry: case Kind::slot: case Kind::when: case Kind::each: case Kind::math: case Kind::mathsrc:
    case Kind::equations: case Kind::fill: case Kind::terms:
      return ObjKind::Error;  // not an object row: the error box
  }
}

void dumpObjects(std::string& out, const HList& h, const Interner& strs, const char* indent) {
  for (size_t i = 0; i < h.objs.size(); i++) {
    const InlineObject& o = h.objs[i];
    appendf(out, "%sobj o%zu %s parts=%u edges=%s/%s", indent, i, objectKind(o.kind).name, o.nParts,
            kCCName[o.firstCC], kCCName[o.lastCC]);
    if (o.deferred) out += " deferred";
    if (o.src) {
      out += " src=\"";
      appendEscaped(out, strs.get(o.src));
      out += '"';
    }
    for (u32 p = o.part0; p < o.part0 + o.nParts; p++) {
      const ObjPart& pt = h.parts[p];
      appendf(out, " [w=%dsu asc=%dsu desc=%dsu", pt.w, pt.asc, pt.desc);
      if (p > o.part0) appendf(out, " glue=%dsu", pt.glueBefore);
      out += ']';
    }
    out += '\n';
  }
}

}  // namespace tsr
