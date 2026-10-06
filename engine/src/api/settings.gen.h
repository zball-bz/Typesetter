// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// Host settings (schema "settings"; plan P1-03, design T4 M3 / T9 A4).
#pragma once
#include <map>
#include <string>
#include <string_view>

#include "../support/support.h"
#include "stages.h"

namespace tsr {

// Adjacent-punctuation compression style (clreq; v2 App C).
//   Full: every adjacent gap compressed (newspaper-tight)
//   Book: close+close and open+open set solid, but a breakable half-width
//         breathing space is kept between a closing/dot and an opening punct
//   None: full-width style — all punctuation spaces kept (rigid where 禁则
//         forbids a break)
enum class PunctCompress : u8 { Full = 0, Book = 1, None = 2 };

// Line cost (document-model §11; TeX-bounded since P0-12): x below
// -shrinkThreshold is Overfull; cost = min(mapped(x)^exponent, cap).
struct CostParams {
  u8 exponent = 3;  // cost.exponent
  double shrinkThreshold = 0.37;  // cost.shrinkThreshold
  double shrinkCoeff = 0.6;  // cost.shrinkCoeff
  double cap = 10000;  // cost.cap
};

// Every host setting, one member per row (defaults = the registry's).
struct Config {
  double widthPx = 300;  // host.width
  double dppx = 1;  // host.dppx
  std::string loadedFaces = "";  // host.loadedFaces
  double epsilonPerWordSu = 1;  // host.epsilonSu
  std::string lang = "zh-CN";  // doc.lang
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
  double mathRelAfterPenalty = 0.8;  // break.mathRelAfter
  double mathRelBeforePenalty = 0.85;  // break.mathRelBefore
  double mathBinAfterPenalty = 0.95;  // break.mathBinAfter
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
  std::string semElements = "";  // semantics.elements
  std::string semCounters = "";  // semantics.counters
  std::string semCollectors = "";  // semantics.collectors
  std::string semSystems = "";  // semantics.systems
  std::string idPrefix = "tsr-";  // render.idPrefix
  std::string styleRules = "";  // style.rules
  CostParams cost;
};

// The settings each stage reads (plan P3-02): a view of Config holding only the
// rows whose `affects` names the stage, so reading any other row is a compile
// error and a settings patch reruns every stage that reads a row. Views hold
// references: the Config outlives them (the Doc owns it).
struct IngestSettings {
  const double& paraIndentEm;  // par.indent
  const double& listIndentEm;  // list.indent
  const double& quoteIndentEm;  // quote.indent
  const double& codeScale;  // code.scale
  const int& verbatimContIndent;  // code.contIndent
  const double& sidebarFrac;  // code.sidecarFrac
  const bool& verbatimSnapKerning;  // code.snapKerning
  const std::string& codeFontFeatures;  // code.fontFeatures
  const std::map<std::string, std::string>& codeFontFeaturesByLang;  // code.fontFeaturesByLang
  const std::string& semElements;  // semantics.elements
  const std::string& semCounters;  // semantics.counters
  const std::string& semCollectors;  // semantics.collectors
  const std::string& semSystems;  // semantics.systems
  const std::string& styleRules;  // style.rules
  IngestSettings(const Config& c)  // NOLINT: a Config is its view
      : paraIndentEm(c.paraIndentEm),
        listIndentEm(c.listIndentEm),
        quoteIndentEm(c.quoteIndentEm),
        codeScale(c.codeScale),
        verbatimContIndent(c.verbatimContIndent),
        sidebarFrac(c.sidebarFrac),
        verbatimSnapKerning(c.verbatimSnapKerning),
        codeFontFeatures(c.codeFontFeatures),
        codeFontFeaturesByLang(c.codeFontFeaturesByLang),
        semElements(c.semElements),
        semCounters(c.semCounters),
        semCollectors(c.semCollectors),
        semSystems(c.semSystems),
        styleRules(c.styleRules) {}
};
struct ResolveSettings {
  const std::string& lang;  // doc.lang
  const std::string& supHeading;  // terms.heading
  const std::string& supTable;  // terms.table
  const std::string& supFigure;  // terms.figure
  const std::string& supEquation;  // terms.equation
  const std::string& capSep;  // terms.captionSep
  ResolveSettings(const Config& c)  // NOLINT: a Config is its view
      : lang(c.lang),
        supHeading(c.supHeading),
        supTable(c.supTable),
        supFigure(c.supFigure),
        supEquation(c.supEquation),
        capSep(c.capSep) {}
};
struct BoxTreeSettings {
  const double& baseSizePx;  // doc.baseSize
  BoxTreeSettings(const Config& c)  // NOLINT: a Config is its view
      : baseSizePx(c.baseSizePx) {}
};
struct EmitSettings {
  const double& epsilonPerWordSu;  // host.epsilonSu
  const double& baseSizePx;  // doc.baseSize
  const double& lineHeight;  // doc.leading
  const double& cjkJustifyK;  // doc.cjkJustify
  const double& cjkGlueEm;  // doc.cjkGlue
  const PunctCompress& punctCompress;  // cjk.punctCompress
  const double& hyphenPenalty;  // break.hyphenPenalty
  const double& urlBreakPenalty;  // break.urlPenalty
  const u32& urlBreakMinLen;  // break.urlMinLen
  const double& mathRelAfterPenalty;  // break.mathRelAfter
  const double& mathRelBeforePenalty;  // break.mathRelBefore
  const double& mathBinAfterPenalty;  // break.mathBinAfter
  EmitSettings(const Config& c)  // NOLINT: a Config is its view
      : epsilonPerWordSu(c.epsilonPerWordSu),
        baseSizePx(c.baseSizePx),
        lineHeight(c.lineHeight),
        cjkJustifyK(c.cjkJustifyK),
        cjkGlueEm(c.cjkGlueEm),
        punctCompress(c.punctCompress),
        hyphenPenalty(c.hyphenPenalty),
        urlBreakPenalty(c.urlBreakPenalty),
        urlBreakMinLen(c.urlBreakMinLen),
        mathRelAfterPenalty(c.mathRelAfterPenalty),
        mathRelBeforePenalty(c.mathRelBeforePenalty),
        mathBinAfterPenalty(c.mathBinAfterPenalty) {}
};
struct MeasureSettings {
  const double& dppx;  // host.dppx
  const std::string& loadedFaces;  // host.loadedFaces
  const double& epsilonPerWordSu;  // host.epsilonSu
  const std::string& lang;  // doc.lang
  const double& baseSizePx;  // doc.baseSize
  const std::string& bodyFont;  // fonts.body
  const std::string& cjkFont;  // fonts.cjk
  const std::string& monoFont;  // fonts.mono
  const std::string& monoCjkFont;  // fonts.monoCjk
  MeasureSettings(const Config& c)  // NOLINT: a Config is its view
      : dppx(c.dppx),
        loadedFaces(c.loadedFaces),
        epsilonPerWordSu(c.epsilonPerWordSu),
        lang(c.lang),
        baseSizePx(c.baseSizePx),
        bodyFont(c.bodyFont),
        cjkFont(c.cjkFont),
        monoFont(c.monoFont),
        monoCjkFont(c.monoCjkFont) {}
};
struct LayoutSettings {
  const double& widthPx;  // host.width
  const double& baseSizePx;  // doc.baseSize
  const double& lineHeight;  // doc.leading
  const double& paraSpacingEm;  // doc.parGap
  const double& cjkJustifyK;  // doc.cjkJustify
  const double& codeScale;  // code.scale
  const int& verbatimMinCols;  // code.minCols
  const double& verbatimSnapTolerance;  // code.snapTolerance
  const int& verbatimSnapMaxQ;  // code.snapMaxQ
  const double& minWrapWidthEm;  // layout.minWrapWidth
  const double& tableCellPadEm;  // table.cellPad
  const double& tableRowPadEm;  // table.rowPad
  const CostParams& cost;  // cost.*
  LayoutSettings(const Config& c)  // NOLINT: a Config is its view
      : widthPx(c.widthPx),
        baseSizePx(c.baseSizePx),
        lineHeight(c.lineHeight),
        paraSpacingEm(c.paraSpacingEm),
        cjkJustifyK(c.cjkJustifyK),
        codeScale(c.codeScale),
        verbatimMinCols(c.verbatimMinCols),
        verbatimSnapTolerance(c.verbatimSnapTolerance),
        verbatimSnapMaxQ(c.verbatimSnapMaxQ),
        minWrapWidthEm(c.minWrapWidthEm),
        tableCellPadEm(c.tableCellPadEm),
        tableRowPadEm(c.tableRowPadEm),
        cost(c.cost) {}
};
struct PaginateSettings {
  const double& pageHeightPx;  // page.height
  PaginateSettings(const Config& c)  // NOLINT: a Config is its view
      : pageHeightPx(c.pageHeightPx) {}
};
struct PaintSettings {
  const double& widthPx;  // host.width
  const std::string& lang;  // doc.lang
  const double& baseSizePx;  // doc.baseSize
  const double& lineHeight;  // doc.leading
  const double& paraSpacingEm;  // doc.parGap
  const std::string& bodyFont;  // fonts.body
  const std::string& cjkFont;  // fonts.cjk
  const std::string& monoFont;  // fonts.mono
  const std::string& monoCjkFont;  // fonts.monoCjk
  const std::string& idPrefix;  // render.idPrefix
  PaintSettings(const Config& c)  // NOLINT: a Config is its view
      : widthPx(c.widthPx),
        lang(c.lang),
        baseSizePx(c.baseSizePx),
        lineHeight(c.lineHeight),
        paraSpacingEm(c.paraSpacingEm),
        bodyFont(c.bodyFont),
        cjkFont(c.cjkFont),
        monoFont(c.monoFont),
        monoCjkFont(c.monoCjkFont),
        idPrefix(c.idPrefix) {}
};

// host policy (schema "policy"): how hosts drive the engine
constexpr u32 kPolicyMaxRounds = 64;  // pull-loop rounds before a typeset is declared stalled
constexpr u32 kPolicyFontDeadlineMs = 4000;  // declared fonts measure as their fallback after this
constexpr u32 kPolicyFontRetryMs = 30000;  // a failed font load is retried after this
constexpr u32 kPolicyGrammarRetryMs = 30000;  // a failed highlight grammar load is retried after this
constexpr u32 kPolicyImageTimeoutMs = 15000;  // main-thread image size fallback timeout
constexpr u32 kPolicySessionBudgetBytes = 67108864;  // the worker Session's answer cache and memo budget (content-keyed: widths, vertical metrics, code tokens, KP results)
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
