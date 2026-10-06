// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// Host settings (schema "settings"; plan P1-03, design T4 M3 / T9 A4): Config,
// policy and the settings codec — the host's (api/); stages read their views
// (settings_views.gen.h).
#pragma once
#include <map>
#include <string>
#include <string_view>

#include "settings_views.gen.h"

namespace tsr {

// Every host setting, one member per row (defaults = the registry's).
struct Config {
  double widthPx = 300;  // host.width
  double dppx = 1;  // host.dppx
  std::string loadedFaces = "";  // host.loadedFaces
  double epsilonPerWordSu = 1;  // host.epsilonSu
  std::string lang = "auto";  // doc.lang
  double baseSizePx = 18;  // doc.baseSize
  double lineHeight = 1.5;  // doc.leading
  double paraSpacingEm = 1.2;  // doc.parGap
  double cjkJustifyK = 0.6;  // doc.cjkJustify
  double cjkGlueEm = 0.1;  // doc.cjkGlue
  std::string bodyFont = "\"Crimson Text\", Georgia, serif";  // fonts.body
  std::string cjkFont = "\"Noto Serif CJK SC\", \"Source Han Serif SC\", \"Songti SC\", SimSun, serif";  // fonts.cjk
  std::string monoFont = "monospace";  // fonts.mono
  std::string monoCjkFont = "";  // fonts.monoCjk
  double paraIndentEm = 0;  // par.indent
  double listIndentEm = 1.5;  // list.indent
  double quoteIndentEm = 1;  // quote.indent
  PunctCompress punctCompress = PunctCompress::Book;  // cjk.punctCompress
  double hyphenPenalty = 0.7;  // break.hyphenPenalty
  double urlBreakPenalty = 1.2;  // break.urlPenalty
  u32 urlBreakMinLen = 20;  // break.urlMinLen
  ClassMap mathBreakAfter = ClassMap{{-1, -1, 0.95, 0.8, -1, -1, -1, -1}};  // math.breakAfter
  ClassMap mathBreakBefore = ClassMap{{-1, -1, -1, 0.85, -1, -1, -1, -1}};  // math.breakBefore
  double codeScale = 0.85;  // code.scale
  int verbatimContIndent = 2;  // code.contIndent
  double sidebarFrac = 0.4;  // code.sidecarFrac
  bool verbatimSnapKerning = false;  // code.snapKerning
  int verbatimMinCols = 8;  // code.minCols
  double verbatimSnapTolerance = 0.1;  // code.snapTolerance
  int verbatimSnapMaxQ = 7;  // code.snapMaxQ
  std::string codeFontFeatures = "";  // code.fontFeatures
  std::map<std::string, std::string> codeFontFeaturesByLang = {};  // code.fontFeaturesByLang
  double minWrapWidthEm = 8;  // layout.minWrapWidth
  double tableCellPadEm = 0.4;  // table.cellPad
  double tableRowPadEm = 0.3;  // table.rowPad
  std::string supHeading = "";  // terms.heading
  std::string supTable = "";  // terms.table
  std::string supFigure = "";  // terms.figure
  std::string supEquation = "";  // terms.equation
  std::string capSep = "";  // terms.captionSep
  double pageHeightPx = 995;  // page.height
  double pageWidthPx = 666;  // page.width
  double pageMarginPx = 64;  // page.margin
  std::string semElements = "";  // semantics.elements
  std::string semCounters = "";  // semantics.counters
  std::string semCollectors = "";  // semantics.collectors
  std::string semHtml = "";  // semantics.html
  std::string semSystems = "";  // semantics.systems
  std::string projectDoc = "";  // project.doc
  std::string projectStarts = "";  // project.starts
  std::string projectUrls = "";  // project.urls
  std::string idPrefix = "tsr-";  // render.idPrefix
  u8 renderMath = kRenderMathBoxes;  // render.math
  bool a11yMathLabel = true;  // a11y.mathLabel
  bool a11yTextLayer = false;  // a11y.textLayer
  std::string styleRules = "";  // style.rules
  CostParams cost;
};


// host policy (schema "policy"): how hosts drive the engine
constexpr u32 kPolicyMaxRounds = 64;  // pull-loop rounds before a typeset is declared stalled
constexpr u32 kPolicyFontDeadlineMs = 4000;  // declared fonts measure as their fallback after this
constexpr u32 kPolicyFontRetryMs = 30000;  // a failed font load is retried after this
constexpr u32 kPolicyGrammarRetryMs = 30000;  // a failed highlight grammar load is retried after this
constexpr u32 kPolicyImageTimeoutMs = 15000;  // main-thread image size fallback timeout
constexpr u32 kPolicySessionBudgetBytes = 67108864;  // the worker Session's answer cache and memo budget (content-keyed: widths, vertical metrics, code tokens, KP results)
constexpr u32 kPolicyResourceCacheEntries = 512;  // the resource host's LRU cache of URL-keyed answers (image sizes, loads): entries kept
constexpr u32 kPolicyResourceCacheBytes = 33554432;  // the resource host's LRU cache: bytes of loaded resources kept
constexpr u32 kPolicyResourceTtlMs = 300000;  // a cached resource is revalidated (ETag, Last-Modified; a file's mtime and size) after this
constexpr u32 kPolicyResourceFailureTtlMs = 10000;  // a failed resource (an image that did not load, a missing file) is retried after this
constexpr double kPolicyNativeImagePx[] = {512, 384};  // the native/golden image provider's answer

// the result of a settings document: which stages its applied rows affect
struct SettingsPatch {
  bool ok = true;       // the document parsed
  u32 applied = 0;      // rows applied
  u32 affects = 0;      // stageBit() set of the applied rows
};
// applies a JSON settings document onto c in row order; unknown paths and
// bad values are diagnostics (the row keeps its value)
SettingsPatch applySettings(Config& c, std::string_view json, DiagSink& diags);
// the effective settings as one JSON document (every row)
std::string settingsJson(const Config& c);

}  // namespace tsr
