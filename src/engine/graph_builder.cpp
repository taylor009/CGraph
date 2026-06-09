#include "cgraph/graph_builder.hpp"

#include "cgraph/normalize.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cgraph {
namespace {

constexpr std::string_view kCallRelation = "CALLS";

[[nodiscard]] std::string node_key(const Node& node) {
  if (!node.id.empty()) {
    return node.id;
  }
  return make_id(node.source_file + ":" + node.kind + ":" + node.label);
}

[[nodiscard]] std::string edge_key(const Edge& edge) {
  return edge.source + "\n" + edge.relation + "\n" + edge.target;
}

// Language built-in callables. Graphify never resolves a call to one of these
// names to a project node (a `new Map()` or `console`/`parseInt` call must not
// be wired to a coincidentally same-named user symbol), so we skip them too.
[[nodiscard]] bool is_builtin_global(std::string_view label) {
  static const std::unordered_set<std::string_view> names = {
      // JavaScript / TypeScript ECMAScript built-ins
      "String", "Number", "Boolean", "Object", "Array", "Symbol", "BigInt",
      "Date", "RegExp", "Error", "TypeError", "RangeError", "SyntaxError",
      "ReferenceError", "EvalError", "URIError",
      "Promise", "Map", "Set", "WeakMap", "WeakSet", "JSON", "Math",
      "Reflect", "Proxy", "Intl",
      "parseInt", "parseFloat", "isNaN", "isFinite",
      "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
      // Browser / Node common globals
      "URL", "URLSearchParams", "FormData", "Blob", "File",
      "Headers", "Request", "Response", "AbortController", "AbortSignal",
      "TextEncoder", "TextDecoder", "console",
      // Python built-in callables
      "str", "int", "float", "bool", "list", "dict", "set", "tuple", "bytes",
      "len", "range", "enumerate", "zip", "map", "filter", "sum", "min", "max",
      "print", "open", "isinstance", "type", "super", "sorted", "reversed",
      "any", "all", "abs", "round", "next", "iter", "hash", "id", "repr",
      "callable", "getattr", "setattr", "hasattr", "delattr", "vars", "dir",
  };
  return names.contains(label);
}

[[nodiscard]] std::unordered_map<std::string, std::vector<std::string>> label_index(const GraphSnapshot& graph) {
  std::unordered_map<std::string, std::vector<std::string>> index;
  for (const auto& node : graph.nodes) {
    index[make_id(node.label)].push_back(node.id);
  }
  return index;
}

}  // namespace

GraphSnapshot merge_fragments(std::span<const Fragment> fragments) {
  GraphSnapshot graph;
  graph.build_state = BuildState::DeterministicReady;
  for (const auto& fragment : fragments) {
    merge_fragment(graph, fragment);
  }
  return graph;
}

void merge_fragment(GraphSnapshot& graph, const Fragment& fragment) {
  std::unordered_set<std::string> seen_node_ids;
  std::unordered_set<std::string> graph_node_ids;
  std::unordered_set<std::string> graph_edge_ids;

  for (const auto& node : graph.nodes) {
    graph_node_ids.insert(node.id);
  }
  for (const auto& edge : graph.edges) {
    graph_edge_ids.insert(edge_key(edge));
  }

  for (auto node : fragment.nodes) {
    node.id = node_key(node);
    if (!seen_node_ids.insert(node.id).second) {
      continue;
    }
    if (graph_node_ids.insert(node.id).second) {
      graph.nodes.push_back(std::move(node));
    }
  }

  for (const auto& edge : fragment.edges) {
    const auto key = edge_key(edge);
    if (graph_edge_ids.insert(key).second) {
      graph.edges.push_back(edge);
    }
  }

  for (const auto& hyperedge : fragment.hyperedges) {
    const auto duplicate = std::ranges::any_of(graph.hyperedges, [&hyperedge](const Hyperedge& existing) {
      return existing.id == hyperedge.id;
    });
    if (!duplicate) {
      graph.hyperedges.push_back(hyperedge);
    }
  }
}

void resolve_imports(GraphSnapshot& graph, std::span<const PathAlias> aliases) {
  namespace fs = std::filesystem;

  // Index every project file by its extension-stripped path, and index.* files
  // also by their directory, so specifiers that omit the extension or point at a
  // package directory ("./utils", "../lib/foo") resolve to the real file node.
  std::unordered_map<std::string, std::string> file_id_by_key;
  std::unordered_map<std::string, std::string> source_of_file;
  for (const auto& node : graph.nodes) {
    if (node.kind != "file") {
      continue;
    }
    source_of_file.emplace(node.id, node.source_file);
    const fs::path path = fs::path(node.source_file).lexically_normal();
    file_id_by_key[path.generic_string()] = node.id;
    file_id_by_key.emplace((path.parent_path() / path.stem()).generic_string(), node.id);
    if (path.stem() == "index") {
      file_id_by_key.emplace(path.parent_path().generic_string(), node.id);
    }
  }

  const auto lookup = [&](const std::string& candidate) -> std::optional<std::string> {
    const fs::path path = fs::path(candidate).lexically_normal();
    if (const auto found = file_id_by_key.find(path.generic_string()); found != file_id_by_key.end()) {
      return found->second;
    }
    // TypeScript NodeNext imports spell the extension as ".js" while the source
    // file is ".ts"; fall back to the extension-stripped stem.
    if (const auto stem = (path.parent_path() / path.stem()).generic_string();
        stem != path.generic_string()) {
      if (const auto found = file_id_by_key.find(stem); found != file_id_by_key.end()) {
        return found->second;
      }
    }
    return std::nullopt;
  };

  const auto resolve_file = [&](const std::string& import_path) -> std::optional<std::string> {
    if (import_path.empty()) {
      return std::nullopt;
    }
    if (const auto direct = lookup(import_path)) {
      return direct;
    }
    // A bare specifier may be a tsconfig path alias (`@/lib/utils`). Expand it to
    // its real project path and retry — resolving dependencies a relative-only
    // resolver (and Graphify) leaves dangling.
    for (const auto& candidate : expand_path_alias(aliases, import_path)) {
      if (const auto resolved = lookup(candidate)) {
        return resolved;
      }
    }
    return std::nullopt;
  };

  std::unordered_set<std::string> node_ids;
  node_ids.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    node_ids.insert(node.id);
  }

  // Map each resolvable stub onto the real file (module) or declared symbol
  // (import) it refers to. Stubs that resolve to no project file are third-party
  // package imports (`import {x} from "react"`); Graphify never materialises
  // those, so we drop the stub and its edges rather than leaving leaf clutter.
  std::unordered_map<std::string, std::string> remap;
  std::unordered_set<std::string> removed;
  std::unordered_set<std::string> dropped;
  for (const auto& node : graph.nodes) {
    if (node.kind != "import" && node.kind != "module") {
      continue;
    }
    const auto prop = node.properties.find("import_path");
    if (prop == node.properties.end()) {
      continue;
    }
    const auto file_id = resolve_file(prop->second);
    if (!file_id) {
      removed.insert(node.id);
      dropped.insert(node.id);  // external package: delete node and its edges
      continue;
    }
    if (node.kind == "module") {
      remap[node.id] = *file_id;
    } else {
      const auto real = make_id(source_of_file[*file_id] + ":" + node.label);
      remap[node.id] = node_ids.contains(real) ? real : *file_id;
    }
    removed.insert(node.id);
  }
  if (removed.empty()) {
    return;
  }

  const auto canonical = [&](const std::string& id) {
    const auto found = remap.find(id);
    return found == remap.end() ? id : found->second;
  };
  std::unordered_set<std::string> seen_edges;
  std::vector<Edge> rewritten;
  rewritten.reserve(graph.edges.size());
  for (auto edge : graph.edges) {
    if (dropped.contains(edge.source) || dropped.contains(edge.target)) {
      continue;  // edge to a dropped third-party import
    }
    edge.source = canonical(edge.source);
    edge.target = canonical(edge.target);
    if (edge.source == edge.target) {
      continue;  // a file importing from itself collapses to a self-edge
    }
    if (seen_edges.insert(edge_key(edge)).second) {
      rewritten.push_back(std::move(edge));
    }
  }
  graph.edges = std::move(rewritten);
  std::erase_if(graph.nodes, [&removed](const Node& node) { return removed.contains(node.id); });
}

void resolve_raw_calls(GraphSnapshot& graph, std::span<const RawCall> raw_calls) {
  const auto index = label_index(graph);
  std::unordered_set<std::string> node_ids;
  std::unordered_map<std::string, std::string> source_file_by_id;
  node_ids.reserve(graph.nodes.size());
  source_file_by_id.reserve(graph.nodes.size());

  // Per-file declared symbols: source_file -> (normalized label -> node id). An
  // empty id marks a label that is declared more than once in the file, so it
  // resolves to no single target.
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> local_by_file;
  std::unordered_map<std::string, std::string> label_by_id;
  for (const auto& node : graph.nodes) {
    node_ids.insert(node.id);
    source_file_by_id.emplace(node.id, node.source_file);
    label_by_id.emplace(node.id, node.label);
    if (node.source_file.empty()) {
      continue;
    }
    if (node.kind != "function" && node.kind != "class" && node.kind != "type" && node.kind != "variable") {
      continue;
    }
    auto& by_label = local_by_file[node.source_file];
    const auto [slot, inserted] = by_label.emplace(make_id(node.label), node.id);
    if (!inserted && slot->second != node.id) {
      slot->second.clear();  // ambiguous within the file
    }
  }

  // Import evidence per caller file id: the set of symbol targets it imports and
  // the set of module (file) targets it imports from. This only grades the
  // confidence of a resolved call (EXTRACTED when the callee was imported,
  // INFERRED otherwise) — it never resolves a call. Resolution is purely by
  // name, exactly as Graphify does: a call is wired by same-file declaration or
  // by a project-wide unique label, and an import that is ambiguous or missing
  // changes only the confidence, not whether the edge exists.
  std::unordered_map<std::string, std::unordered_set<std::string>> imported_symbols;
  std::unordered_map<std::string, std::unordered_set<std::string>> imported_modules;
  for (const auto& edge : graph.edges) {
    if (edge.relation == "imports" || edge.relation == "re_exports") {
      imported_symbols[edge.source].insert(edge.target);
    } else if (edge.relation == "imports_from") {
      imported_modules[edge.source].insert(edge.target);
    }
  }

  // Seed the dedupe set with existing edges so resolution is O(calls), not
  // O(calls * edges).
  std::unordered_set<std::string> seen_edges;
  seen_edges.reserve(graph.edges.size());
  for (const auto& edge : graph.edges) {
    seen_edges.insert(edge_key(edge));
  }

  for (const auto& raw_call : raw_calls) {
    if (raw_call.callee_label.empty() || is_builtin_global(raw_call.callee_label)) {
      continue;
    }
    // The caller must resolve to a real graph node. Module-level calls with no
    // enclosing symbol are skipped rather than attached to a synthetic file id
    // that would dangle (no consumer can render an edge to a missing node).
    if (raw_call.caller_id.empty() || !node_ids.contains(raw_call.caller_id)) {
      continue;
    }
    const auto key = make_id(raw_call.callee_label);
    const auto& caller_file = source_file_by_id[raw_call.caller_id];

    std::string target_id;
    auto confidence = Confidence::Extracted;

    // 1. A symbol declared in the caller's own file (local helper, sibling fn).
    if (const auto file = local_by_file.find(caller_file); file != local_by_file.end()) {
      if (const auto slot = file->second.find(key); slot != file->second.end()) {
        target_id = slot->second;
      }
    }

    // 2. A project-wide unique label. An ambiguous name (more than one
    //    declaration) or an unknown one resolves to nothing and is dropped —
    //    this exactly-one-candidate rule is what keeps cross-file calls honest.
    //    Member calls (`obj.method()`) are excluded: the bare property name has
    //    no import evidence and collides with any top-level function of the same
    //    name, so it stays scoped to the caller's own file (handled above).
    if (target_id.empty() && !raw_call.is_member_call) {
      const auto targets = index.find(key);
      if (targets == index.end() || targets->second.size() != 1) {
        continue;
      }
      target_id = targets->second.front();
      // Grade confidence: EXTRACTED when the caller's file actually imports the
      // resolved symbol or its module, INFERRED when it is only a name match.
      const auto caller_file_id = make_id(caller_file);
      const auto callee_file_id = make_id(source_file_by_id[target_id]);
      const auto symbols = imported_symbols.find(caller_file_id);
      const auto modules = imported_modules.find(caller_file_id);
      const bool has_import_evidence =
          (symbols != imported_symbols.end() && symbols->second.contains(target_id)) ||
          (modules != imported_modules.end() && modules->second.contains(callee_file_id));
      confidence = has_import_evidence ? Confidence::Extracted : Confidence::Inferred;
    }

    if (target_id.empty() || target_id == raw_call.caller_id) {
      continue;  // unresolved or self-edge
    }
    Edge edge{
        .source = raw_call.caller_id,
        .target = target_id,
        .relation = std::string(kCallRelation),
        .confidence = confidence,
    };
    if (seen_edges.insert(edge_key(edge)).second) {
      graph.edges.push_back(std::move(edge));
    }
  }
}

void resolve_raw_relations(GraphSnapshot& graph, std::span<const RawRelation> raw_relations) {
  std::unordered_set<std::string> node_ids;
  std::unordered_map<std::string, std::string> label_by_id;
  node_ids.reserve(graph.nodes.size());

  // Per-file declared symbols (label -> id, empty when ambiguous), the same
  // index used for same-file call resolution. Heritage relations may resolve a
  // base type to a declaration in the same file.
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> local_by_file;
  for (const auto& node : graph.nodes) {
    node_ids.insert(node.id);
    label_by_id.emplace(node.id, node.label);
    if (node.source_file.empty()) {
      continue;
    }
    if (node.kind != "function" && node.kind != "class" && node.kind != "type" && node.kind != "variable") {
      continue;
    }
    auto& by_label = local_by_file[node.source_file];
    const auto [slot, inserted] = by_label.emplace(make_id(node.label), node.id);
    if (!inserted && slot->second != node.id) {
      slot->second.clear();
    }
  }

  // Per-file imported names (file id -> label -> imported target id), built from
  // the import/re_export edges left by resolve_imports. This is the import-alias
  // map every relation target is resolved through.
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> imported_by_file;
  for (const auto& edge : graph.edges) {
    if (edge.relation != "imports" && edge.relation != "re_exports") {
      continue;
    }
    if (const auto label = label_by_id.find(edge.target); label != label_by_id.end()) {
      imported_by_file[edge.source].emplace(make_id(label->second), edge.target);
    }
  }

  std::unordered_set<std::string> seen_edges;
  seen_edges.reserve(graph.edges.size());
  for (const auto& edge : graph.edges) {
    seen_edges.insert(edge_key(edge));
  }

  for (const auto& relation : raw_relations) {
    if (relation.source_id.empty() || relation.target_label.empty() || !node_ids.contains(relation.source_id)) {
      continue;
    }
    const auto key = make_id(relation.target_label);

    std::string target_id;
    // 1. The type the source file imports (the canonical resolution path).
    if (const auto file = imported_by_file.find(make_id(relation.source_file)); file != imported_by_file.end()) {
      if (const auto slot = file->second.find(key); slot != file->second.end()) {
        target_id = slot->second;
      }
    }
    // 2. Heritage may also resolve to a same-file declaration (`class A extends
    //    B` where B is declared in the same module and not imported).
    if (target_id.empty() && relation.allow_same_file) {
      if (const auto file = local_by_file.find(relation.source_file); file != local_by_file.end()) {
        if (const auto slot = file->second.find(key); slot != file->second.end()) {
          target_id = slot->second;
        }
      }
    }

    if (target_id.empty() || target_id == relation.source_id) {
      continue;  // unresolvable (third-party / same-file reference) or self-edge
    }
    Edge edge{
        .source = relation.source_id,
        .target = target_id,
        .relation = relation.relation,
        .confidence = Confidence::Extracted,
    };
    if (!relation.context.empty()) {
      edge.properties.emplace("context", relation.context);
    }
    if (seen_edges.insert(edge_key(edge)).second) {
      graph.edges.push_back(std::move(edge));
    }
  }
}

}  // namespace cgraph
