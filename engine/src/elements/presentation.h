// The presentation map (plan P3-23; design T7 PresentationMap): how a kind,
// a generated role or an element class shows on each page — the semantic
// page's element, its parts' elements (slots), its ARIA role, a structural
// projection — and the typeset page's hooks (a data-role attribute on its
// blocks, a frame). Its rows are engine/data/elements.json's `html` section,
// patched by the host (semantics.html) and by a document ($.element(name,
// {html})); a class without a row of its own reads its `like:` base's.
// Every element, role and projection is checked against the allowlists
// below when the registry loads (D-R09): a row naming anything else refuses
// the registry.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../model/model.h"

namespace tsr {

struct HtmlShape {
  std::string name;
  // the element ("" with an inline row: none, the content as it is);
  // `levelSuffix`: the element's name ends with the node's level argument,
  // clamped to 1–6 (h{level})
  std::string element;
  bool levelSuffix = false;
  bool inlineLevel = false;
  // the structural projections (code; any row may select one)
  enum class Projection : u8 { None, List, Table, Codeblock, Math, Term } projection = Projection::None;
  // a part (a kid with a slot) → the element it is written in
  std::vector<std::pair<SlotId, std::string>> slots;
  std::string aria;  // the element's role attribute ("" none)
  // what an inline element says of a run already (a run whose own style
  // says it needs no second element for it): sup, strong/b, em/i
  enum class Says : u8 { Nothing, Super, Bold, Italic } says = Says::Nothing;
  // the typeset page: data-role on its blocks (D-R02), a frame (D-Y11)
  bool dataRole = false;
  bool frame = false;

  std::string_view slotElement(SlotId s) const {
    for (const auto& [id, el] : slots)
      if (id == s) return el;
    return {};
  }
};

// the elements a row may name: flow and sectioning elements for a block
// row, phrasing elements for an inline one — never a raw-text, embedded,
// interactive or metadata element (script, style, iframe, img, a, …)
inline constexpr std::string_view kHtmlBlockElements[] = {
    "p",     "div",        "section", "article", "aside", "nav", "header", "footer", "main", "figure", "figcaption",
    "blockquote", "ul",   "ol",      "li",      "dl",    "dt",  "dd",     "pre",    "hr",   "table",  "address",
    "details", "summary", "hgroup",  "h"};
inline constexpr std::string_view kHtmlPhrasingElements[] = {
    "sup", "sub", "strong", "b", "em", "i", "small", "span", "code", "kbd", "samp", "var",
    "mark", "cite", "q", "abbr", "dfn", "s", "u", "del", "ins", "bdi", "time", "data"};
// the ARIA roles a row may give (WAI-ARIA 1.2 document structure, DPUB-ARIA)
inline constexpr std::string_view kAriaRoles[] = {
    "note", "region", "navigation", "complementary", "figure", "group", "math", "definition", "term", "list",
    "listitem", "doc-endnotes", "doc-footnote", "doc-bibliography", "doc-toc", "doc-index", "doc-glossary",
    "doc-example", "doc-tip", "doc-notice"};

}  // namespace tsr
