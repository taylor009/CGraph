#include "cgraph/semantic_connectivity.hpp"

#include <cmath>
#include <string>

namespace {

cgraph::Node node(std::string id) {
  cgraph::Node n;
  n.id = std::move(id);
  return n;
}

cgraph::Edge edge(std::string source, std::string target) {
  cgraph::Edge e;
  e.source = std::move(source);
  e.target = std::move(target);
  e.relation = "REL";
  return e;
}

bool approx(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

int main() {
  using namespace cgraph;

  // --- direct doc -> code: connected at hop_bound 1 ---
  {
    GraphSnapshot g;
    g.nodes = {node("doc:a"), node("code:x")};
    g.edges = {edge("doc:a", "code:x")};
    const auto c = compute_semantic_connectivity(g, 1);
    if (c.doc_nodes != 1 || c.connected_docs != 1 || c.orphan_docs != 0 || c.doc_code_edges != 1) {
      return 1;
    }
    if (!approx(c.connectivity_rate, 1.0)) {
      return 2;
    }
  }

  // --- doc -> concept -> code: connected at 2 hops, NOT at 1 (transitive + bound) ---
  {
    GraphSnapshot g;
    g.nodes = {node("doc:a"), node("concept:k"), node("code:x")};
    g.edges = {edge("doc:a", "concept:k"), edge("concept:k", "code:x")};
    const auto at1 = compute_semantic_connectivity(g, 1);
    const auto at2 = compute_semantic_connectivity(g, 2);
    if (at1.connected_docs != 0 || at1.orphan_docs != 1) {
      return 3;  // not reachable within 1 hop
    }
    if (at2.connected_docs != 1 || at2.orphan_docs != 0) {
      return 4;  // reachable through the concept within 2
    }
    // concept:k touches code -> not an orphan concept; the bridge edge counts.
    if (at2.concept_nodes != 1 || at2.orphan_concepts != 0 || at2.doc_code_edges != 1) {
      return 5;  // concept->code is the only semantic->code edge
    }
  }

  // --- doc -> concept with a code-less concept: orphan doc AND orphan concept ---
  {
    GraphSnapshot g;
    g.nodes = {node("doc:a"), node("concept:floating")};
    g.edges = {edge("doc:a", "concept:floating")};
    const auto c = compute_semantic_connectivity(g, 2);
    if (c.connected_docs != 0 || c.orphan_docs != 1) {
      return 6;
    }
    if (c.orphan_concepts != 1 || c.doc_code_edges != 0) {
      return 7;  // nothing bridges into code
    }
  }

  // --- doc_code_edges counts only semantic->code, not semantic->semantic ---
  {
    GraphSnapshot g;
    g.nodes = {node("doc:a"), node("concept:k"), node("code:x")};
    g.edges = {edge("doc:a", "concept:k"), edge("doc:a", "code:x"), edge("concept:k", "code:x")};
    const auto c = compute_semantic_connectivity(g, 2);
    if (c.doc_code_edges != 2) {
      return 8;  // doc:a->code:x and concept:k->code:x; doc:a->concept:k excluded
    }
  }

  // --- pure code graph: zero semantic, rate 0, no divide by zero ---
  {
    GraphSnapshot g;
    g.nodes = {node("code:x"), node("code:y")};
    g.edges = {edge("code:x", "code:y")};
    const auto c = compute_semantic_connectivity(g, 2);
    if (c.doc_nodes != 0 || c.concept_nodes != 0 || c.doc_code_edges != 0 || !approx(c.connectivity_rate, 0.0)) {
      return 9;
    }
  }

  // --- topic: is counted with concepts ---
  {
    GraphSnapshot g;
    g.nodes = {node("topic:t"), node("code:x")};
    g.edges = {edge("topic:t", "code:x")};
    const auto c = compute_semantic_connectivity(g, 1);
    if (c.concept_nodes != 1 || c.orphan_concepts != 0) {
      return 10;
    }
  }

  return 0;
}
