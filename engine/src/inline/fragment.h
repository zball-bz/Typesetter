// Re-entrant inline fragment parsing (verbatim-design §5; the engine half
// of the long-deferred m.parse). A runtime string parses with the SAME
// inline grammar as document prose — math islands, emphasis, code spans,
// links, @refs — and converts directly to ContentNodes (no JS round-trip).
// Splices are NOT evaluated (no executor here): a `#…` stays literal text
// with an Info diagnostic.
#pragma once
#include "../model/model.h"

namespace tsr {

// All produced nodes carry `span` (the generating construct's span) and
// styles composed onto `baseStyle`. Refs are plain `ref{target}` nodes —
// run this BEFORE the resolver if they should resolve.
std::vector<ContentNode*> parseInlineFragment(std::string_view text,
                                              StyleId baseStyle, Span span,
                                              Arena& arena, Interner& strs,
                                              StyleTable& styles,
                                              DiagSink& diags);

}  // namespace tsr
