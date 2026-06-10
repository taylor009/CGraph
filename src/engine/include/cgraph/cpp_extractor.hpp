#pragma once

#include "cgraph/extractor.hpp"

namespace cgraph {

// Relation/structure handlers shared by the C and C++ configs. They give the
// C-family extractor the same relation richness the JS/TS extractor already has:
//
//  - cpp_import_handler:   `#include` -> an `imports` edge (file -> included file),
//                          resolved to the real project file by resolve_imports;
//                          system/third-party includes resolve to nothing and are
//                          dropped (no dangling edges).
//  - cpp_relation_handler: base classes -> `inherits`, and member/parameter/return
//                          types -> `references` (resolved via includes).
//  - cpp_field_walk:       data members of a struct/class -> `field` nodes with a
//                          `defines` edge from the owning type.
void cpp_import_handler(const TSNode& node, const ExtractionContext& context, Fragment& fragment);
void cpp_relation_handler(const TSNode& node, const ExtractionContext& context, const std::string& node_id, std::vector<RawRelation>& out);
void cpp_field_walk(const TSNode& node, const ExtractionContext& context, Fragment& fragment, std::vector<RawCall>& raw_calls);

}  // namespace cgraph
