#pragma once

#include "cgraph/extractor.hpp"
#include "cgraph/tsconfig_aliases.hpp"
#include "cgraph/types.hpp"

#include <span>

namespace cgraph {

[[nodiscard]] GraphSnapshot merge_fragments(std::span<const Fragment> fragments);
void merge_fragment(GraphSnapshot& graph, const Fragment& fragment);

// Relinks import/module stub nodes (kind "import"/"module", carrying an
// `import_path` property) onto the real project file and declared symbol they
// refer to, then drops the now-redundant stubs. Imports to files outside the
// graph (e.g. third-party packages) are left as stubs. Run after merge_fragments
// and before resolve_raw_calls so call resolution can use a file's resolved
// imports.
void resolve_imports(GraphSnapshot& graph, std::span<const PathAlias> aliases = {});

void resolve_raw_calls(GraphSnapshot& graph, std::span<const RawCall> raw_calls);

// Resolves type/heritage facts (inherits/implements/references) onto real graph
// edges. Each target type name is resolved against the source file's imports
// and, for heritage relations only, a same-file declaration — never a
// project-wide name guess. Unresolvable targets emit no edge. Run after
// resolve_imports so a file's imports point at their real symbols.
void resolve_raw_relations(GraphSnapshot& graph, std::span<const RawRelation> raw_relations);

}  // namespace cgraph
