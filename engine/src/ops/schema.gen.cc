// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#include "schema.gen.h"

namespace tsr {
namespace {
const AttrSpec kA_heading[] = {
    {1, "level", Dom::Int, 1, 6, nullptr, nullptr, 0, false, true, 1, 6},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_list[] = {
    {2, "ordered", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, true, 0, 6},
    {3, "start", Dom::Int, -1073741824, 1073741824, nullptr, nullptr, 0, false, true, 1, 6}};
const AttrSpec kA_codeblock[] = {
    {4, "lang", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {24, "wrap", Dom::Bool, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {25, "lineNo", Dom::Int, 0, 1048576, nullptr, nullptr, 0, true, false, 0, 6},
    {26, "hl", Dom::RangeSet, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {27, "sidecar", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_group[] = {
    {6, "role", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_table[] = {
    {7, "cols", Dom::Int, 1, 64, nullptr, nullptr, 0, false, false, 0, 6},
    {8, "align", Dom::Token, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_term[] = {
    {9, "name", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const char* const kM_collect_what[] = {"toc", "glossary", "notes", "bibliography"};
const char* const kM_collect_form[] = {"all"};
const AttrSpec kA_collect[] = {
    {10, "what", Dom::Enum, 0, 0, kM_collect_what, nullptr, 4, false, false, 0, 6},
    {16, "form", Dom::Enum, 0, 0, kM_collect_form, nullptr, 1, false, false, 0, 6}};
const AttrSpec kA_mathblock[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {0, "label", Dom::Label, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_error[] = {
    {12, "message", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {13, "code", Dom::Ident, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const char* const kM_styled_bits[] = {"EM", "BOLD", "UNDER", "OVER", "STRIKE"};
const std::uint8_t kB_styled_bits[] = {2, 3, 16, 17, 18};
const AttrSpec kA_styled[] = {
    {20, "bits", Dom::Flags, 0, 0, kM_styled_bits, kB_styled_bits, 5, false, true, 0, 6},
    {21, "font", Dom::Font, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {4, "lang", Dom::Lang, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {22, "color", Dom::Color, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {23, "sizePx", Dom::Num, 1, 2000, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_link[] = {
    {14, "url", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_ref[] = {
    {15, "target", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_mathinline[] = {
    {11, "src", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6}};
const AttrSpec kA_raw[] = {
    {17, "html", Dom::Html, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {18, "w", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6},
    {19, "h", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6}};
const char* const kM_image_side[] = {"left", "right"};
const AttrSpec kA_image[] = {
    {11, "src", Dom::Url, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {29, "alt", Dom::Str, 0, 0, nullptr, nullptr, 0, false, false, 0, 6},
    {18, "w", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6},
    {19, "h", Dom::Num, 0, 100000, nullptr, nullptr, 0, false, false, 0, 6},
    {28, "scale", Dom::Num, 0, 100, nullptr, nullptr, 0, false, false, 0, 6},
    {30, "side", Dom::Enum, 0, 0, kM_image_side, nullptr, 2, false, false, 0, 6}};
}  // namespace

const KindInfo kKinds[KIND_COUNT] = {
    {"doc", Level::Block, Body::Blocks, 6, nullptr, 0},
    {"para", Level::Block, Body::Inline, 6, nullptr, 0},
    {"heading", Level::Block, Body::Inline, 6, kA_heading, 2},
    {"list", Level::Block, Body::Items, 6, kA_list, 2},
    {"item", Level::Block, Body::Blocks, 6, nullptr, 0},
    {"quote", Level::Block, Body::Blocks, 6, nullptr, 0},
    {"codeblock", Level::Block, Body::Code, 6, kA_codeblock, 5},
    {"rule", Level::Block, Body::None, 6, nullptr, 0},
    {"group", Level::Adaptive, Body::Position, 6, kA_group, 3},
    {"table", Level::Block, Body::Rows, 6, kA_table, 3},
    {"trow", Level::Block, Body::Cells, 6, nullptr, 0},
    {"tcell", Level::Block, Body::Inline, 6, nullptr, 0},
    {"term", Level::Adaptive, Body::Inline, 6, kA_term, 1},
    {"collect", Level::Block, Body::Data, 6, kA_collect, 2},
    {"mathblock", Level::Block, Body::None, 6, kA_mathblock, 2},
    {"error", Level::Adaptive, Body::None, 6, kA_error, 2},
    {"comment", Level::Trivia, Body::Text, 6, nullptr, 0},
    {"text", Level::Inline, Body::None, 6, nullptr, 0},
    {"styled", Level::Transparent, Body::Position, 6, kA_styled, 5},
    {"link", Level::Inline, Body::Inline, 6, kA_link, 1},
    {"code", Level::Inline, Body::Text, 6, nullptr, 0},
    {"ref", Level::Inline, Body::None, 6, kA_ref, 1},
    {"mathinline", Level::Inline, Body::None, 6, kA_mathinline, 1},
    {"raw", Level::Block, Body::None, 6, kA_raw, 3},
    {"hardbreak", Level::Inline, Body::None, 6, nullptr, 0},
    {"seq", Level::Transparent, Body::Position, 6, nullptr, 0},
    {"image", Level::Block, Body::None, 6, kA_image, 6},
    {"note", Level::Inline, Body::Blocks, 6, nullptr, 0}};

}  // namespace tsr
