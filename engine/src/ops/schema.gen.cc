// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#include "schema.gen.h"

namespace tsr {
namespace {
const AttrSpec kA_para[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_heading[] = {
    {1, "level", Dom::Int, 1, 6, nullptr, nullptr, 0, false, true, 1, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_list[] = {
    {2, "ordered", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, true, 0, 6, 0, false},
    {3, "start", Dom::Int, -1073741824, 1073741824, nullptr, nullptr, 0, false, true, 1, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_item[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_quote[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_codeblock[] = {
    {4, "lang", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {24, "wrap", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {25, "lineNo", Dom::Int, 0, 1048576, nullptr, nullptr, 0, true, false, 0, 6, 0, false},
    {26, "hl", Dom::RangeSet, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {27, "sidecar", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_rule[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_group[] = {
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_table[] = {
    {7, "cols", Dom::Int, 1, 64, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {8, "align", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_trow[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_tcell[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_term[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const char* const kM_collect_what[] = {"toc", "glossary", "notes", "bibliography"};
const char* const kM_collect_form[] = {"all"};
const AttrSpec kA_collect[] = {
    {10, "what", Dom::Enum, 0, 0, kM_collect_what, nullptr, 4, false, false, 0, 6, 0, false},
    {16, "form", Dom::Enum, 0, 0, kM_collect_form, nullptr, 1, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_mathblock[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, true},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_error[] = {
    {12, "message", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {13, "code", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_comment[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const char* const kM_styled_bits[] = {"EM", "BOLD", "UNDER", "OVER", "STRIKE"};
const std::uint8_t kB_styled_bits[] = {2, 3, 16, 17, 18};
const AttrSpec kA_styled[] = {
    {20, "bits", Dom::Flags, 0, 0, kM_styled_bits, kB_styled_bits, 5, false, true, 0, 6, 0, false},
    {21, "font", Dom::Font, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {4, "lang", Dom::Lang, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {22, "color", Dom::Color, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {23, "sizePx", Dom::Num, 1, 2000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_link[] = {
    {14, "url", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_code[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_ref[] = {
    {15, "target", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {14, "url", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, true},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_mathinline[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_raw[] = {
    {17, "html", Dom::Html, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {18, "w", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {19, "h", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_hardbreak[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_seq[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const char* const kM_image_side[] = {"left", "right"};
const AttrSpec kA_image[] = {
    {11, "src", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {29, "alt", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {18, "w", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {19, "h", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {28, "scale", Dom::Num, 0, 100, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {30, "side", Dom::Enum, 0, 0, kM_image_side, nullptr, 2, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_note[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
const AttrSpec kA_field[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {36, "of", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 6, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 7, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false}};
}  // namespace

const KindInfo kKinds[KIND_COUNT] = {
    {"doc", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, nullptr, 0},
    {"para", Level::Block, Body::Inline, InlineShape::Container, 6, kA_para, 7},
    {"heading", Level::Block, Body::Inline, InlineShape::Unsupported, 6, kA_heading, 8},
    {"list", Level::Block, Body::Items, InlineShape::Unsupported, 6, kA_list, 9},
    {"item", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, kA_item, 7},
    {"quote", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, kA_quote, 7},
    {"codeblock", Level::Block, Body::Code, InlineShape::Unsupported, 6, kA_codeblock, 12},
    {"rule", Level::Block, Body::None, InlineShape::Unsupported, 6, kA_rule, 7},
    {"group", Level::Adaptive, Body::Position, InlineShape::Container, 6, kA_group, 8},
    {"table", Level::Block, Body::Rows, InlineShape::Unsupported, 6, kA_table, 9},
    {"trow", Level::Block, Body::Cells, InlineShape::Unsupported, 6, kA_trow, 7},
    {"tcell", Level::Block, Body::Inline, InlineShape::Unsupported, 6, kA_tcell, 7},
    {"term", Level::Adaptive, Body::Inline, InlineShape::Unsupported, 6, kA_term, 8},
    {"collect", Level::Block, Body::Data, InlineShape::Unsupported, 6, kA_collect, 9},
    {"mathblock", Level::Block, Body::None, InlineShape::Unsupported, 6, kA_mathblock, 9},
    {"error", Level::Adaptive, Body::None, InlineShape::Error, 6, kA_error, 9},
    {"comment", Level::Trivia, Body::Text, InlineShape::Skip, 6, kA_comment, 7},
    {"text", Level::Inline, Body::None, InlineShape::Text, 6, nullptr, 0},
    {"styled", Level::Transparent, Body::Position, InlineShape::Container, 6, kA_styled, 12},
    {"link", Level::Inline, Body::Inline, InlineShape::Container, 6, kA_link, 8},
    {"code", Level::Inline, Body::Text, InlineShape::Code, 6, kA_code, 7},
    {"ref", Level::Inline, Body::None, InlineShape::Container, 6, kA_ref, 9},
    {"mathinline", Level::Inline, Body::None, InlineShape::Object, 6, kA_mathinline, 8},
    {"raw", Level::Block, Body::None, InlineShape::Object, 6, kA_raw, 10},
    {"hardbreak", Level::Inline, Body::None, InlineShape::Break, 6, kA_hardbreak, 7},
    {"seq", Level::Transparent, Body::Position, InlineShape::Container, 6, kA_seq, 7},
    {"image", Level::Block, Body::None, InlineShape::Object, 6, kA_image, 13},
    {"note", Level::Inline, Body::Blocks, InlineShape::Unsupported, 6, kA_note, 7},
    {"field", Level::Inline, Body::None, InlineShape::Skip, 9, kA_field, 9}};

const DeclInfo kDecls[DECL_COUNT] = {
    {nullptr, false, 0},
    {"element", true, 9},
    {"counter", true, 9},
    {"collector", true, 9},
    {"counter-system", true, 9},
    {"doc", true, 9},
    {"locale", true, 9},
    {"fontRoles", true, 9},
    {"rule", false, 9},
    {"math.symbol", false, 9},
    {"math.op", false, 9},
    {"math.fn", false, 9}};

}  // namespace tsr
