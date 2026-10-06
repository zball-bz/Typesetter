// PHASE 0 (plan P2-07; design T3 "Semantic declarations", "Element
// registry"): the registry a document resolves with. Three layers patch the
// rows of engine/data/elements.json's form, named by section and name, field
// by field (the last write wins; a new name appends a row): the built-in rows
// < the host's semantics.* settings < the document's declarations (DECL
// element / counter / collector / counter-system, hoisted: the document's
// last declaration of a name). A declaration's row is its EXT `row` — the
// JS stdlib's canonical JSON, a template written {"$t": k} for its k-th
// template, converted from its raw nodes — else its EXT data as the row's
// scalar fields. A row the loader refuses is dropped (decl-invalid at the
// declaration, semantics-invalid for a host row) and the others stand.
// Registries are cached by their inputs: a document executed again with the
// same declarations builds none.
#pragma once
#include <memory>

#include "../api/settings.gen.h"
#include "../elements/registry.h"

namespace tsr {

// `base`: a replacement for the built-in rows (tests), "" = the built-in ones
std::shared_ptr<const Registry> declaredRegistry(const RawOps& raw, const IngestSettings& cfg, std::string_view base,
                                                 DiagSink& diags);

// after instantiate: an instance before its class's declaration
// (decl-after-use, info) and an event no EMIT reaches (event-unplaced: a
// counterUpdate whose value was discarded)
void checkDeclarations(const RawOps& raw, const ContentTree& tree, const Registry& reg, const Interner& strs,
                       DiagSink& diags);

// a template's raw nodes in elements.json's template form (exposed for tests)
bool templateJson(const RawOps& raw, u32 id, JsonValue& out, std::string& error);

}  // namespace tsr
