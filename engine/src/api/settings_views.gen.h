// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The settings each stage reads (schema "settings"; plans P1-03, P3-02, P3-32).
// Stage code includes this header, never settings.gen.h: Config is only
// declared here, so a stage reads a row through its view or not at all.
#pragma once
#include <array>
#include <map>
#include <string>
#include <string_view>

#include "../support/support.h"
#include "stages.h"

namespace tsr {

// a value per TeX atom class (ord, op, bin, rel, open, close, punct, inner); -1: none (plan P3-25)
using ClassMap = std::array<double, 8>;

// render.math (plan P3-27: an enum setting is its value's index)
inline constexpr u8 kRenderMathBoxes = 0;
inline constexpr u8 kRenderMathSource = 1;

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

// Every host setting (settings.gen.h: the host's; a stage reads its view)
struct Config;

// The settings each stage reads (plan P3-02): a view of Config holding only the
// rows whose `affects` names the stage, so reading any other row is a compile
// error and a settings patch reruns every stage that reads a row. Views hold
// references: the Config outlives them (the Doc owns it). Built from a Config
// by the host (settings.gen.cc).
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
  const std::string& semHtml;  // semantics.html
  const std::string& semSystems;  // semantics.systems
  const std::string& styleRules;  // style.rules
  IngestSettings(const Config& c);  // NOLINT: a Config is its view
};
struct ResolveSettings {
  const std::string& lang;  // doc.lang
  const std::string& supHeading;  // terms.heading
  const std::string& supTable;  // terms.table
  const std::string& supFigure;  // terms.figure
  const std::string& supEquation;  // terms.equation
  const std::string& capSep;  // terms.captionSep
  const std::string& projectDoc;  // project.doc
  const std::string& projectStarts;  // project.starts
  ResolveSettings(const Config& c);  // NOLINT: a Config is its view
};
struct BoxTreeSettings {
  const std::string& lang;  // doc.lang
  const double& baseSizePx;  // doc.baseSize
  BoxTreeSettings(const Config& c);  // NOLINT: a Config is its view
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
  const ClassMap& mathBreakAfter;  // math.breakAfter
  const ClassMap& mathBreakBefore;  // math.breakBefore
  EmitSettings(const Config& c);  // NOLINT: a Config is its view
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
  MeasureSettings(const Config& c);  // NOLINT: a Config is its view
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
  LayoutSettings(const Config& c);  // NOLINT: a Config is its view
};
struct PaginateSettings {
  const double& pageHeightPx;  // page.height
  const double& pageWidthPx;  // page.width
  const double& pageMarginPx;  // page.margin
  PaginateSettings(const Config& c);  // NOLINT: a Config is its view
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
  const std::string& projectUrls;  // project.urls
  const std::string& idPrefix;  // render.idPrefix
  const bool& runWidths;  // render.runWidths
  const u8& renderMath;  // render.math
  const bool& a11yMathLabel;  // a11y.mathLabel
  const bool& a11yTextLayer;  // a11y.textLayer
  PaintSettings(const Config& c);  // NOLINT: a Config is its view
};

}  // namespace tsr
