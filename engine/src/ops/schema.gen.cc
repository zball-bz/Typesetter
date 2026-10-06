// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#include "schema.gen.h"

namespace tsr {
namespace {
const char* const kM_para_attach[] = {"prev", "next", "both"};
const AttrSpec kA_para[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_para_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_heading_attach[] = {"prev", "next", "both"};
const AttrSpec kA_heading[] = {
    {1, "level", Dom::Int, 1, 6, nullptr, nullptr, 0, false, true, 1, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_heading_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_list_attach[] = {"prev", "next", "both"};
const AttrSpec kA_list[] = {
    {2, "ordered", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, true, 0, 6, 0, false},
    {3, "start", Dom::Int, -1073741824, 1073741824, nullptr, nullptr, 0, false, true, 1, 6, 0, false},
    {41, "numbering", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_list_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_item_attach[] = {"prev", "next", "both"};
const AttrSpec kA_item[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_item_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_quote_attach[] = {"prev", "next", "both"};
const AttrSpec kA_quote[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_quote_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_codeblock_attach[] = {"prev", "next", "both"};
const AttrSpec kA_codeblock[] = {
    {4, "lang", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {24, "wrap", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {25, "lineNo", Dom::Int, 0, 1048576, nullptr, nullptr, 0, true, false, 0, 6, 0, false},
    {26, "hl", Dom::RangeSet, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {27, "sidecar", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {70, "snapKerning", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {71, "sidecarFrac", Dom::Num, 0.1, 0.9, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {72, "contIndent", Dom::Int, 0, 40, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {73, "features", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 14, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_codeblock_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_rule_attach[] = {"prev", "next", "both"};
const AttrSpec kA_rule[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_rule_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_group_attach[] = {"prev", "next", "both"};
const AttrSpec kA_group[] = {
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {75, "kind", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_group_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_table_attach[] = {"prev", "next", "both"};
const AttrSpec kA_table[] = {
    {7, "cols", Dom::Int, 1, 64, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {8, "align", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_table_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_trow_attach[] = {"prev", "next", "both"};
const AttrSpec kA_trow[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_trow_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_tcell_attach[] = {"prev", "next", "both"};
const AttrSpec kA_tcell[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_tcell_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_term_attach[] = {"prev", "next", "both"};
const AttrSpec kA_term[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_term_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_collect_form[] = {"all"};
const char* const kM_collect_cited[] = {"cited", "cited-then-all"};
const char* const kM_collect_attach[] = {"prev", "next", "both"};
const AttrSpec kA_collect[] = {
    {10, "what", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {16, "form", Dom::Enum, 0, 0, kM_collect_form, nullptr, 1, false, false, 0, 6, 0, false},
    {46, "cited", Dom::Enum, 0, 0, kM_collect_cited, nullptr, 2, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_collect_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_mathblock_attach[] = {"prev", "next", "both"};
const AttrSpec kA_mathblock[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, true},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_mathblock_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_error_attach[] = {"prev", "next", "both"};
const AttrSpec kA_error[] = {
    {12, "message", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {13, "code", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_error_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_comment_attach[] = {"prev", "next", "both"};
const AttrSpec kA_comment[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_comment_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_styled_decoration[] = {"UNDER", "OVER", "STRIKE"};
const std::uint8_t kB_styled_decoration[] = {0, 1, 2};
const char* const kM_styled_fontRole[] = {"body", "mono"};
const char* const kM_styled_baseline[] = {"super", "sub"};
const char* const kM_styled_hang[] = {"indent", "content"};
const char* const kM_styled_parAlign[] = {"justify", "start", "center", "end"};
const char* const kM_styled_parHyphenate[] = {"auto", "true", "false"};
const char* const kM_styled_punct[] = {"full", "book", "none"};
const char* const kM_styled_attach[] = {"prev", "next", "both"};
const AttrSpec kA_styled[] = {
    {21, "font", Dom::Font, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {4, "lang", Dom::Lang, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {22, "color", Dom::Color, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {23, "sizePx", Dom::Num, 1, 2000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {47, "weight", Dom::Int, 100, 900, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {48, "italic", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {49, "decoration", Dom::Flags, 0, 0, kM_styled_decoration, kB_styled_decoration, 3, false, false, 0, 11, 0, false},
    {50, "fontRole", Dom::Enum, 0, 0, kM_styled_fontRole, nullptr, 2, false, false, 0, 11, 0, false},
    {51, "baseline", Dom::Enum, 0, 0, kM_styled_baseline, nullptr, 2, false, false, 0, 11, 0, false},
    {52, "size", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 3, false},
    {53, "hang", Dom::Enum, 0, 0, kM_styled_hang, nullptr, 2, false, false, 0, 11, 0, false},
    {57, "parIndent", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 11, false},
    {58, "parAlign", Dom::Enum, 0, 0, kM_styled_parAlign, nullptr, 4, false, false, 0, 12, 0, false},
    {59, "parHyphenate", Dom::Enum, 0, 0, kM_styled_parHyphenate, nullptr, 3, false, false, 0, 12, 0, false},
    {60, "blockGap", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 12, false},
    {61, "blockIndent", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 11, false},
    {62, "keepWithNext", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {63, "listMarker", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {64, "matchKind", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {65, "matchRole", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {66, "matchClass", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {67, "matchLang", Dom::Lang, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {68, "matchDepth", Dom::Int, 1, 16, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {69, "matchWhere", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 13, false},
    {70, "snapKerning", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {71, "sidecarFrac", Dom::Num, 0.1, 0.9, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {72, "contIndent", Dom::Int, 0, 40, nullptr, nullptr, 0, false, false, 0, 13, 0, false},
    {73, "features", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 13, 14, false},
    {74, "punct", Dom::Enum, 0, 0, kM_styled_punct, nullptr, 3, false, false, 0, 13, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_styled_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_link_attach[] = {"prev", "next", "both"};
const AttrSpec kA_link[] = {
    {14, "url", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_link_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_code_attach[] = {"prev", "next", "both"};
const AttrSpec kA_code[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_code_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_ref_attach[] = {"prev", "next", "both"};
const AttrSpec kA_ref[] = {
    {15, "target", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {14, "url", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, true},
    {16, "form", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {42, "supplement", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_ref_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_mathinline_attach[] = {"prev", "next", "both"};
const AttrSpec kA_mathinline[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_mathinline_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_raw_attach[] = {"prev", "next", "both"};
const AttrSpec kA_raw[] = {
    {17, "html", Dom::Html, 0, 0, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {18, "w", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {19, "h", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_raw_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_hardbreak_attach[] = {"prev", "next", "both"};
const AttrSpec kA_hardbreak[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_hardbreak_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_seq_attach[] = {"prev", "next", "both"};
const AttrSpec kA_seq[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_seq_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_image_side[] = {"left", "right"};
const char* const kM_image_attach[] = {"prev", "next", "both"};
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
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_image_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_note_attach[] = {"prev", "next", "both"};
const AttrSpec kA_note[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_note_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_field_attach[] = {"prev", "next", "both"};
const AttrSpec kA_field[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {36, "of", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_field_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_event_attach[] = {"prev", "next", "both"};
const AttrSpec kA_event[] = {
    {37, "counter", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {38, "set", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 4, false},
    {39, "step", Dom::Int, 1, 16, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {40, "add", Dom::Int, -1073741824, 1073741824, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {41, "numbering", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {42, "supplement", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_event_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_entry_attach[] = {"prev", "next", "both"};
const AttrSpec kA_entry[] = {
    {43, "key", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_entry_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_slot_attach[] = {"prev", "next", "both"};
const AttrSpec kA_slot[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {45, "or", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_slot_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_when_attach[] = {"prev", "next", "both"};
const AttrSpec kA_when[] = {
    {36, "of", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_when_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_each_attach[] = {"prev", "next", "both"};
const AttrSpec kA_each[] = {
    {36, "of", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {44, "sep", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 10, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_each_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_math_attach[] = {"prev", "next", "both"};
const AttrSpec kA_math[] = {
    {56, "display", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_math_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_mathsrc_attach[] = {"prev", "next", "both"};
const AttrSpec kA_mathsrc[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 12, 0, false},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_mathsrc_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_equations_attach[] = {"prev", "next", "both"};
const AttrSpec kA_equations[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_equations_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const char* const kM_fill_attach[] = {"prev", "next", "both"};
const AttrSpec kA_fill[] = {
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {31, "slot", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {32, "syn", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {33, "copy", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 8, false},
    {34, "class", Dom::Text, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 9, false},
    {35, "ext", Dom::Ext, 0, 0, nullptr, nullptr, 0, false, false, 0, 9, 0, false},
    {54, "style", Dom::Delta, 0, 0, nullptr, nullptr, 0, false, false, 0, 11, 0, false},
    {55, "attach", Dom::Enum, 0, 0, kM_fill_attach, nullptr, 3, false, false, 0, 11, 0, false}};
const std::uint16_t kS_margin[] = {6};
const std::uint16_t kS_extra[] = {21};
const std::uint16_t kS_caption[] = {8};
}  // namespace

const KindInfo kKinds[KIND_COUNT] = {
    {"doc", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, nullptr, 0},
    {"para", Level::Block, Body::Inline, InlineShape::Container, 6, kA_para, 9},
    {"heading", Level::Block, Body::Inline, InlineShape::Unsupported, 6, kA_heading, 10},
    {"list", Level::Block, Body::Items, InlineShape::Unsupported, 6, kA_list, 12},
    {"item", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, kA_item, 9},
    {"quote", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, kA_quote, 9},
    {"codeblock", Level::Block, Body::Code, InlineShape::Unsupported, 6, kA_codeblock, 18},
    {"rule", Level::Block, Body::None, InlineShape::Unsupported, 6, kA_rule, 9},
    {"group", Level::Adaptive, Body::Position, InlineShape::Container, 6, kA_group, 11},
    {"table", Level::Block, Body::Rows, InlineShape::Unsupported, 6, kA_table, 11},
    {"trow", Level::Block, Body::Cells, InlineShape::Unsupported, 6, kA_trow, 9},
    {"tcell", Level::Block, Body::Blocks, InlineShape::Unsupported, 6, kA_tcell, 9},
    {"term", Level::Adaptive, Body::Inline, InlineShape::Unsupported, 6, kA_term, 10},
    {"collect", Level::Block, Body::Data, InlineShape::Unsupported, 6, kA_collect, 12},
    {"mathblock", Level::Block, Body::None, InlineShape::Unsupported, 6, kA_mathblock, 11},
    {"error", Level::Adaptive, Body::None, InlineShape::Error, 6, kA_error, 11},
    {"comment", Level::Trivia, Body::Text, InlineShape::Skip, 6, kA_comment, 9},
    {"text", Level::Inline, Body::None, InlineShape::Text, 6, nullptr, 0},
    {"styled", Level::Transparent, Body::Position, InlineShape::Container, 6, kA_styled, 38},
    {"link", Level::Inline, Body::Inline, InlineShape::Container, 6, kA_link, 10},
    {"code", Level::Inline, Body::Text, InlineShape::Code, 6, kA_code, 9},
    {"ref", Level::Inline, Body::None, InlineShape::Container, 6, kA_ref, 13},
    {"mathinline", Level::Inline, Body::None, InlineShape::Object, 6, kA_mathinline, 10},
    {"raw", Level::Adaptive, Body::None, InlineShape::Object, 6, kA_raw, 12},
    {"hardbreak", Level::Inline, Body::None, InlineShape::Break, 6, kA_hardbreak, 9},
    {"seq", Level::Transparent, Body::Position, InlineShape::Container, 6, kA_seq, 9},
    {"image", Level::Adaptive, Body::None, InlineShape::Object, 6, kA_image, 15},
    {"note", Level::Inline, Body::Blocks, InlineShape::Unsupported, 6, kA_note, 9},
    {"field", Level::Inline, Body::None, InlineShape::Skip, 9, kA_field, 11},
    {"event", Level::Trivia, Body::None, InlineShape::Skip, 10, kA_event, 15},
    {"entry", Level::Trivia, Body::Inline, InlineShape::Skip, 10, kA_entry, 10},
    {"slot", Level::Inline, Body::None, InlineShape::Skip, 10, kA_slot, 11},
    {"when", Level::Transparent, Body::Position, InlineShape::Container, 10, kA_when, 10},
    {"each", Level::Transparent, Body::Position, InlineShape::Container, 10, kA_each, 11},
    {"math", Level::Inline, Body::Data, InlineShape::Object, 12, kA_math, 10},
    {"mathsrc", Level::Trivia, Body::None, InlineShape::Skip, 12, kA_mathsrc, 10},
    {"equations", Level::Block, Body::Blocks, InlineShape::Unsupported, 12, kA_equations, 9},
    {"fill", Level::Inline, Body::None, InlineShape::Fill, 12, kA_fill, 9}};

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

const SlotInfo kSlots[SLOT_COUNT] = {
    {nullptr, Body::None, false, nullptr, 0},
    {"margin", Body::Data, false, kS_margin, 1},
    {"extra", Body::Inline, false, kS_extra, 1},
    {"tag", Body::Inline, true, nullptr, 0},
    {"caption", Body::Inline, false, kS_caption, 1}};

}  // namespace tsr
