#include "cgraph/export_json.hpp"

#include "cgraph/fragment_json.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>

namespace cgraph {
namespace {

[[nodiscard]] std::string html_escape(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
      case '&':
        output += "&amp;";
        break;
      case '<':
        output += "&lt;";
        break;
      case '>':
        output += "&gt;";
        break;
      case '"':
        output += "&quot;";
        break;
      case '\'':
        output += "&#39;";
        break;
      default:
        output.push_back(ch);
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string cypher_escape(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '\\' || ch == '\'') {
      output.push_back('\\');
    }
    output.push_back(ch);
  }
  return output;
}

[[nodiscard]] std::unordered_map<std::string, std::size_t> node_index(const GraphSnapshot& graph) {
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    index.emplace(graph.nodes[i].id, i);
  }
  return index;
}

[[nodiscard]] std::string json_for_script(const nlohmann::json& value) {
  auto json = value.dump();
  std::string output;
  output.reserve(json.size());
  for (std::size_t index = 0; index < json.size(); ++index) {
    if (json[index] == '<' && index + 1 < json.size() && json[index + 1] == '/') {
      output += "<\\/";
      ++index;
      continue;
    }
    output.push_back(json[index]);
  }
  return output;
}

}  // namespace

nlohmann::json to_node_link_json(const GraphSnapshot& graph) {
  auto nodes = nlohmann::json::array();
  for (const auto& node : graph.nodes) {
    auto value = to_json(node);
    value["id"] = node.id;
    nodes.push_back(std::move(value));
  }

  auto links = nlohmann::json::array();
  for (const auto& edge : graph.edges) {
    auto value = to_json(edge);
    value["source"] = edge.source;
    value["target"] = edge.target;
    links.push_back(std::move(value));
  }

  return nlohmann::json{
      {"directed", true},
      {"multigraph", false},
      {"graph", {{"build_state", static_cast<int>(graph.build_state)}, {"cache_hit_rate", graph.cache_hit_rate}}},
      {"nodes", std::move(nodes)},
      {"links", std::move(links)},
  };
}

std::string export_graph_html(const GraphSnapshot& graph) {
  std::ostringstream output;
  output << R"html(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>cgraph</title>
<style>
  :root {
    color-scheme: light;
    --bg: #f6f7f9;
    --panel: #ffffff;
    --ink: #17202a;
    --muted: #667085;
    --line: #d7dde5;
    --edge: #9aa6b2;
    --class: #2563eb;
    --function: #059669;
    --other: #7c3aed;
    --focus: #f59e0b;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font: 14px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    color: var(--ink);
    background: var(--bg);
  }
  .shell {
    min-height: 100vh;
    display: grid;
    grid-template-rows: auto 1fr;
  }
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 14px 18px;
    border-bottom: 1px solid var(--line);
    background: var(--panel);
  }
  h1 {
    margin: 0;
    font-size: 18px;
    font-weight: 650;
  }
  .stats {
    display: flex;
    gap: 14px;
    color: var(--muted);
    white-space: nowrap;
  }
  main {
    min-height: 0;
    display: grid;
    grid-template-columns: minmax(0, 1fr) 320px;
  }
  .stage {
    position: relative;
    min-height: 620px;
    overflow: hidden;
  }
  #graph-canvas {
    width: 100%;
    height: 100%;
    min-height: 620px;
    display: block;
    background: #fbfcfd;
    cursor: grab;
  }
  #graph-canvas:active {
    cursor: grabbing;
  }
  .hint {
    position: absolute;
    left: 14px;
    bottom: 12px;
    color: var(--muted);
    background: rgba(255, 255, 255, 0.86);
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 6px 8px;
    font-size: 12px;
  }
  .legend {
    position: absolute;
    left: 14px;
    top: 12px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    color: var(--muted);
    background: rgba(255, 255, 255, 0.86);
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 8px 10px;
    font-size: 12px;
  }
  .legend .row {
    display: flex;
    align-items: center;
    gap: 7px;
  }
  .legend .dot {
    width: 11px;
    height: 11px;
    border-radius: 50%;
    flex: none;
  }
  aside {
    min-height: 0;
    border-left: 1px solid var(--line);
    background: var(--panel);
    display: grid;
    grid-template-rows: auto 1fr;
  }
  .tools {
    padding: 14px;
    border-bottom: 1px solid var(--line);
  }
  label {
    display: block;
    margin-bottom: 6px;
    color: var(--muted);
    font-size: 12px;
    font-weight: 600;
  }
  input {
    width: 100%;
    min-width: 0;
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 9px 10px;
    font: inherit;
  }
  #node-details {
    min-width: 0;
    overflow: auto;
    padding: 14px;
  }
  .detail-title {
    margin: 0 0 6px;
    font-size: 16px;
    font-weight: 650;
    overflow-wrap: anywhere;
  }
  .detail-kind {
    display: inline-block;
    margin-bottom: 12px;
    color: var(--muted);
  }
  .field {
    margin: 12px 0;
  }
  .field strong {
    display: block;
    margin-bottom: 3px;
    color: var(--muted);
    font-size: 12px;
  }
  .field code {
    display: block;
    overflow-wrap: anywhere;
    white-space: normal;
  }
  .empty {
    color: var(--muted);
  }
  @media (max-width: 840px) {
    main {
      grid-template-columns: 1fr;
      grid-template-rows: minmax(520px, 60vh) auto;
    }
    aside {
      border-left: 0;
      border-top: 1px solid var(--line);
    }
    header {
      align-items: flex-start;
      flex-direction: column;
    }
  }
</style>
</head>
<body>
<div class="shell">
<header>
  <h1>cgraph</h1>
  <div class="stats">
)html";
  output << "    <span>Nodes: " << graph.nodes.size() << "</span>\n";
  output << "    <span>Edges: " << graph.edges.size() << "</span>\n";
  output << R"html(  </div>
</header>
<main>
  <section class="stage" aria-label="Graph visualization">
    <canvas id="graph-canvas" aria-label="Interactive graph visualization"></canvas>
    <div class="legend">
      <div class="row"><span class="dot" style="background:#2563eb"></span>Color = community</div>
      <div class="row"><span class="dot" style="background:#9aa6b2"></span>Bigger = more connected</div>
    </div>
    <div class="hint">Drag a node to rearrange. Scroll to zoom, drag canvas to pan. Click a node for details.</div>
  </section>
  <aside>
    <div class="tools">
      <label for="search">Search nodes</label>
      <input id="search" type="search" placeholder="Search nodes">
    </div>
    <div id="node-details">
      <p class="empty">Select a node to inspect its source, id, and neighbors.</p>
    </div>
  </aside>
</main>
</div>
<script>
)html";
  output << "const graphData = " << json_for_script(to_node_link_json(graph)) << ";\n";
  output << R"html(
const canvas = document.getElementById("graph-canvas");
const ctx = canvas.getContext("2d");
const search = document.getElementById("search");
const details = document.getElementById("node-details");
const nodes = graphData.nodes.map((node, index) => ({...node, index}));
const links = graphData.links || [];
const nodeById = new Map(nodes.map(node => [node.id, node]));
const palette = {
  class: "#2563eb",
  function: "#059669",
  other: "#7c3aed",
  edge: "#9aa6b2",
  text: "#111827",
  muted: "#667085",
  focus: "#f59e0b"
};
let selectedId = "";
let hoverId = "";
let searchTerm = "";
let transform = {x: 0, y: 0, scale: 1};
let dragging = false;
let draggingNodeId = "";
let dragStart = {x: 0, y: 0, tx: 0, ty: 0, nodeX: 0, nodeY: 0};
let layoutReady = false;
const outgoing = new Map();
const incoming = new Map();
const degree = new Map();
for (const node of nodes) degree.set(node.id, 0);
for (const link of links) {
  if (!outgoing.has(link.source)) outgoing.set(link.source, []);
  if (!incoming.has(link.target)) incoming.set(link.target, []);
  outgoing.get(link.source).push(link);
  incoming.get(link.target).push(link);
  degree.set(link.source, (degree.get(link.source) || 0) + 1);
  degree.set(link.target, (degree.get(link.target) || 0) + 1);
}

function radiusFor(node) {
  // Importance-weighted sizing: connected and "god" nodes read larger,
  // matching Graphify's emphasis on hub symbols.
  const deg = degree.get(node.id) || 0;
  const god = node && node.properties && String(node.properties.god_node) === "true";
  const base = 6 + Math.sqrt(deg) * 3.2;
  return Math.min(22, god ? base + 4 : base);
}

function colorFor(node) {
  // Color by community so clusters read as distinct regions, matching
  // Graphify's community-tinted layout. Falls back to node kind when a node has
  // no community assignment.
  const community = communityFor(node);
  if (community !== "") {
    let hash = 0;
    for (let i = 0; i < community.length; ++i) hash = (hash * 31 + community.charCodeAt(i)) | 0;
    return communityPalette[Math.abs(hash) % communityPalette.length];
  }
  const kind = String(node.type || node.kind || "").toLowerCase();
  if (kind.includes("class")) return palette.class;
  if (kind.includes("function") || kind.includes("method")) return palette.function;
  return palette.other;
}

// Distinct, evenly-spread hues so adjacent communities are easy to tell apart.
const communityPalette = [
  "#2563eb", "#059669", "#f59e0b", "#dc2626", "#7c3aed", "#0891b2",
  "#db2777", "#65a30d", "#ea580c", "#0d9488", "#9333ea", "#ca8a04",
  "#e11d48", "#16a34a", "#4f46e5", "#c026d3", "#0284c7", "#84cc16"
];

function shortLabel(value) {
  const label = String(value || "");
  return label.length > 28 ? label.slice(0, 25) + "..." : label;
}

function escapeHtml(value) {
  return String(value || "").replace(/[&<>"']/g, ch => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#39;"
  }[ch]));
}

function communityFor(node) {
  return node && node.properties ? String(node.properties.community || "") : "";
}

// Force-directed (Fruchterman-Reingold style) layout so connected nodes
// cluster and relationships become visible, instead of every node landing on
// one circular ring. Runs as a cooling simulation on animation frames.
let sim = {alpha: 0, k: 60, width: 720, height: 620};

// Deterministic per-node jitter so the same graph lays out the same way each
// load without depending on Math.random.
function seededUnit(seed) {
  const x = Math.sin(seed * 127.1 + 311.7) * 43758.5453;
  return x - Math.floor(x);
}

function layout() {
  if (layoutReady) return;
  const box = canvas.getBoundingClientRect();
  sim.width = Math.max(box.width, 720);
  sim.height = Math.max(box.height, 620);
  const centerX = sim.width / 2;
  const centerY = sim.height / 2;
  // Ideal edge length scales with available area per node.
  sim.k = Math.max(34, Math.sqrt((sim.width * sim.height) / Math.max(nodes.length, 1)));
  // Seed nodes as a filled cloud across the frame so the simulation only has to
  // relax spacing rather than explode outward or collapse inward.
  for (const node of nodes) {
    node.x = centerX + (seededUnit(node.index + 1) - 0.5) * sim.width * 0.72;
    node.y = centerY + (seededUnit(node.index + 13) - 0.5) * sim.height * 0.72;
  }
  sim.alpha = 1;
  layoutReady = true;
}

function simulationTick() {
  const k = sim.k;
  const centerX = sim.width / 2;
  const centerY = sim.height / 2;
  // Repulsion acts only within a cutoff (like a charge distanceMax): interior
  // nodes feel balanced forces and stay put, so the cloud fills the frame
  // instead of stampeding to the walls.
  const cutoff = k * 2.6;
  const cutoffSq = cutoff * cutoff;
  for (const node of nodes) { node.dx = 0; node.dy = 0; }
  for (let i = 0; i < nodes.length; ++i) {
    const a = nodes[i];
    for (let j = i + 1; j < nodes.length; ++j) {
      const b = nodes[j];
      let dx = a.x - b.x;
      let dy = a.y - b.y;
      let distSq = dx * dx + dy * dy;
      if (distSq > cutoffSq) continue;
      if (distSq < 0.01) {
        dx = seededUnit(i * 31 + j) - 0.5;
        dy = seededUnit(j * 31 + i) - 0.5;
        distSq = dx * dx + dy * dy + 0.01;
      }
      const dist = Math.sqrt(distSq);
      const force = (k * k) / dist;
      const fx = (dx / dist) * force;
      const fy = (dy / dist) * force;
      a.dx += fx; a.dy += fy;
      b.dx -= fx; b.dy -= fy;
    }
  }
  // Attraction along edges (springs pulling connected nodes together).
  for (const link of links) {
    const source = nodeById.get(link.source);
    const target = nodeById.get(link.target);
    if (!source || !target) continue;
    const dx = target.x - source.x;
    const dy = target.y - source.y;
    const dist = Math.sqrt(dx * dx + dy * dy) || 0.01;
    const force = (dist * dist) / k;
    const fx = (dx / dist) * force;
    const fy = (dy / dist) * force;
    source.dx += fx; source.dy += fy;
    target.dx -= fx; target.dy -= fy;
  }
  // Moderate gravity centers the whole cloud and counters edge nodes' outward
  // push, keeping the layout filling the interior.
  for (const node of nodes) {
    node.dx += (centerX - node.x) * 0.045;
    node.dy += (centerY - node.y) * 0.045;
  }
  // Move each node by its net displacement, limited by the cooling temperature.
  const maxStep = k * sim.alpha;
  for (const node of nodes) {
    if (node.id === draggingNodeId) continue;
    const disp = Math.sqrt(node.dx * node.dx + node.dy * node.dy);
    if (disp < 0.001) continue;
    const step = Math.min(disp, maxStep);
    node.x += (node.dx / disp) * step;
    node.y += (node.dy / disp) * step;
    node.x = Math.max(24, Math.min(sim.width - 24, node.x));
    node.y = Math.max(24, Math.min(sim.height - 24, node.y));
  }
  sim.alpha *= 0.985;
}

function resizeCanvas() {
  const box = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(box.width, 720);
  const height = Math.max(box.height, 620);
  canvas.width = Math.floor(width * ratio);
  canvas.height = Math.floor(height * ratio);
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
}

function relatedIds(id) {
  const related = new Set([id]);
  for (const link of outgoing.get(id) || []) related.add(link.target);
  for (const link of incoming.get(id) || []) related.add(link.source);
  return related;
}

function highlightIdsFor(id) {
  if (!id) return new Set();
  const node = nodeById.get(id);
  const highlighted = relatedIds(id);
  const community = communityFor(node);
  if (community) {
    for (const candidate of nodes) {
      if (communityFor(candidate) === community) {
        highlighted.add(candidate.id);
      }
    }
  }
  return highlighted;
}

function matchesSearch(node) {
  if (!searchTerm) return true;
  return `${node.label || ""} ${node.id || ""} ${node.source_file || ""}`.toLowerCase().includes(searchTerm);
}

function nodeIsDim(node) {
  if (searchTerm && !matchesSearch(node)) return true;
  const activeId = hoverId || selectedId;
  if (!activeId) return false;
  return !highlightIdsFor(activeId).has(node.id);
}

function screenPoint(worldX, worldY) {
  return {
    x: worldX * transform.scale + transform.x,
    y: worldY * transform.scale + transform.y
  };
}

function worldPoint(screenX, screenY) {
  return {
    x: (screenX - transform.x) / transform.scale,
    y: (screenY - transform.y) / transform.scale
  };
}

function draw() {
  const box = canvas.getBoundingClientRect();
  const activeId = hoverId || selectedId;
  const highlighted = highlightIdsFor(activeId);
  ctx.clearRect(0, 0, box.width, box.height);
  ctx.save();
  ctx.translate(transform.x, transform.y);
  ctx.scale(transform.scale, transform.scale);

  for (const link of links) {
    const source = nodeById.get(link.source);
    const target = nodeById.get(link.target);
    if (!source || !target) continue;
    const dim = nodeIsDim(source) || nodeIsDim(target);
    const activeEdge = activeId && highlighted.has(source.id) && highlighted.has(target.id);
    ctx.globalAlpha = dim ? 0.1 : activeEdge ? 0.85 : 0.4;
    ctx.strokeStyle = activeEdge ? palette.focus : palette.edge;
    ctx.lineWidth = (activeEdge ? 2.2 : 1.4) / transform.scale;
    // Stop the line at the target node's rim so the arrowhead sits cleanly.
    const dx = target.x - source.x;
    const dy = target.y - source.y;
    const dist = Math.sqrt(dx * dx + dy * dy) || 1;
    const ux = dx / dist;
    const uy = dy / dist;
    const targetR = radiusFor(target);
    const tipX = target.x - ux * (targetR + 1.5);
    const tipY = target.y - uy * (targetR + 1.5);
    ctx.beginPath();
    ctx.moveTo(source.x + ux * radiusFor(source), source.y + uy * radiusFor(source));
    ctx.lineTo(tipX, tipY);
    ctx.stroke();
    // Arrowhead conveys edge direction (CALLS, IMPORTS, ...).
    const head = (activeEdge ? 9 : 7) / transform.scale;
    const ang = Math.atan2(uy, ux);
    ctx.beginPath();
    ctx.moveTo(tipX, tipY);
    ctx.lineTo(tipX - head * Math.cos(ang - 0.4), tipY - head * Math.sin(ang - 0.4));
    ctx.lineTo(tipX - head * Math.cos(ang + 0.4), tipY - head * Math.sin(ang + 0.4));
    ctx.closePath();
    ctx.fillStyle = activeEdge ? palette.focus : palette.edge;
    ctx.fill();
    // Relation label only on the active node's edges, to avoid clutter.
    if (activeEdge && link.relation) {
      ctx.globalAlpha = 0.9;
      ctx.fillStyle = palette.muted;
      ctx.font = (10 / transform.scale) + "px -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif";
      ctx.fillText(String(link.relation), (source.x + target.x) / 2 + 4, (source.y + target.y) / 2);
    }
  }

  ctx.font = "11px -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif";
  ctx.textBaseline = "middle";
  for (const node of nodes) {
    const dim = nodeIsDim(node);
    const selected = node.id === selectedId;
    const hovered = node.id === hoverId;
    const activeCommunity = activeId && highlighted.has(node.id) && communityFor(node) === communityFor(nodeById.get(activeId));
    ctx.globalAlpha = dim ? 0.18 : 1;

    const r = radiusFor(node) + (hovered ? 2 : 0);

    if (activeCommunity) {
      ctx.beginPath();
      ctx.fillStyle = palette.focus;
      ctx.globalAlpha = dim ? 0.08 : 0.14;
      ctx.arc(node.x, node.y, r + 11, 0, 2 * Math.PI);
      ctx.fill();
      ctx.globalAlpha = dim ? 0.18 : 1;
    }

    ctx.beginPath();
    ctx.fillStyle = colorFor(node);
    ctx.strokeStyle = selected ? palette.focus : "#ffffff";
    ctx.lineWidth = selected ? 4 / transform.scale : 2.5 / transform.scale;
    ctx.arc(node.x, node.y, r, 0, 2 * Math.PI);
    ctx.fill();
    ctx.stroke();

    // Only label hub nodes by default (and anything active/searched) so the
    // canvas does not turn into a wall of text.
    const labelled = !dim && (r >= 9 || hovered || selected || (searchTerm && matchesSearch(node)));
    if (labelled) {
      ctx.fillStyle = palette.text;
      ctx.globalAlpha = dim ? 0.22 : 0.92;
      ctx.fillText(shortLabel(node.label || node.id), node.x + r + 4, node.y + 1);
    }
  }
  ctx.restore();
  ctx.globalAlpha = 1;
}

let animationHandle = 0;
function runSimulation() {
  if (animationHandle) cancelAnimationFrame(animationHandle);
  const tick = () => {
    // A few ticks per frame settle the layout quickly without a visible crawl.
    for (let i = 0; i < 2; ++i) simulationTick();
    draw();
    if (sim.alpha > 0.02) {
      animationHandle = requestAnimationFrame(tick);
    } else {
      animationHandle = 0;
    }
  };
  animationHandle = requestAnimationFrame(tick);
}

function render() {
  resizeCanvas();
  layout();
  runSimulation();
}

function hitNode(event) {
  const rect = canvas.getBoundingClientRect();
  const point = worldPoint(event.clientX - rect.left, event.clientY - rect.top);
  for (let index = nodes.length - 1; index >= 0; --index) {
    const node = nodes[index];
    const dx = point.x - node.x;
    const dy = point.y - node.y;
    if (Math.sqrt(dx * dx + dy * dy) <= radiusFor(node) + 4) {
      return node;
    }
  }
  return null;
}

function selectNode(id) {
  const node = nodeById.get(id);
  if (!node) return;
  selectedId = id;
  const outgoingLinks = outgoing.get(id) || [];
  const incomingLinks = incoming.get(id) || [];
  const community = communityFor(node) || "none";
  details.innerHTML = `
    <h2 class="detail-title">${escapeHtml(node.label || node.id)}</h2>
    <span class="detail-kind">${escapeHtml(node.type || node.kind || "node")}</span>
    <div class="field"><strong>Source</strong><code>${escapeHtml(node.source_file || "unknown")}</code></div>
    <div class="field"><strong>ID</strong><code>${escapeHtml(node.id)}</code></div>
    <div class="field"><strong>Community</strong><code>${escapeHtml(community)}</code></div>
    <div class="field"><strong>Outgoing</strong><code>${outgoingLinks.length}</code></div>
    <div class="field"><strong>Incoming</strong><code>${incomingLinks.length}</code></div>
  `;
  draw();
}

function applySearch() {
  searchTerm = search.value.trim().toLowerCase();
  draw();
}

canvas.addEventListener("pointerdown", event => {
  const node = hitNode(event);
  if (node) {
    selectNode(node.id);
    dragging = true;
    draggingNodeId = node.id;
    dragStart = {x: event.clientX, y: event.clientY, tx: transform.x, ty: transform.y, nodeX: node.x, nodeY: node.y};
    canvas.setPointerCapture(event.pointerId);
    // Reheat the simulation so neighbors follow the dragged node.
    sim.alpha = Math.max(sim.alpha, 0.35);
    if (!animationHandle) runSimulation();
    return;
  }
  dragging = true;
  draggingNodeId = "";
  dragStart = {x: event.clientX, y: event.clientY, tx: transform.x, ty: transform.y};
  canvas.setPointerCapture(event.pointerId);
});

canvas.addEventListener("pointermove", event => {
  if (dragging && draggingNodeId) {
    const node = nodeById.get(draggingNodeId);
    if (node) {
      const rect = canvas.getBoundingClientRect();
      const pointer = worldPoint(event.clientX - rect.left, event.clientY - rect.top);
      const start = worldPoint(dragStart.x - rect.left, dragStart.y - rect.top);
      node.x = dragStart.nodeX + pointer.x - start.x;
      node.y = dragStart.nodeY + pointer.y - start.y;
      hoverId = draggingNodeId;
    }
  } else if (dragging) {
    transform.x = dragStart.tx + event.clientX - dragStart.x;
    transform.y = dragStart.ty + event.clientY - dragStart.y;
  } else {
    const node = hitNode(event);
    hoverId = node ? node.id : "";
  }
  draw();
});

canvas.addEventListener("pointerup", event => {
  dragging = false;
  draggingNodeId = "";
  canvas.releasePointerCapture(event.pointerId);
});

canvas.addEventListener("pointerleave", () => {
  if (!dragging) {
    hoverId = "";
    draw();
  }
});

canvas.addEventListener("wheel", event => {
  event.preventDefault();
  const rect = canvas.getBoundingClientRect();
  const before = worldPoint(event.clientX - rect.left, event.clientY - rect.top);
  const factor = event.deltaY < 0 ? 1.12 : 0.89;
  transform.scale = Math.max(0.25, Math.min(4, transform.scale * factor));
  const after = screenPoint(before.x, before.y);
  transform.x += event.clientX - rect.left - after.x;
  transform.y += event.clientY - rect.top - after.y;
  draw();
}, {passive: false});

search.addEventListener("input", applySearch);
window.addEventListener("resize", render);
render();
</script>
</body>
</html>)html";
  return output.str();
}

std::string export_graph_svg(const GraphSnapshot& graph) {
  const auto count = std::max<std::size_t>(graph.nodes.size(), 1);
  constexpr double center_x = 240.0;
  constexpr double center_y = 180.0;
  constexpr double radius = 120.0;
  constexpr double pi = 3.14159265358979323846;

  std::vector<std::pair<double, double>> positions;
  positions.reserve(graph.nodes.size());
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto angle = (2.0 * pi * static_cast<double>(i)) / static_cast<double>(count);
    positions.emplace_back(center_x + radius * std::cos(angle), center_y + radius * std::sin(angle));
  }

  const auto index = node_index(graph);
  std::ostringstream output;
  output << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 480 360\">";
  output << "<rect width=\"480\" height=\"360\" fill=\"#ffffff\"/>";
  for (const auto& edge : graph.edges) {
    const auto source = index.find(edge.source);
    const auto target = index.find(edge.target);
    if (source == index.end() || target == index.end()) {
      continue;
    }
    const auto [x1, y1] = positions[source->second];
    const auto [x2, y2] = positions[target->second];
    output << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2
           << "\" stroke=\"#8a8f98\" stroke-width=\"1.5\"/>";
  }
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto [x, y] = positions[i];
    output << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"16\" fill=\"#2563eb\"/>";
    output << "<text x=\"" << x << "\" y=\"" << (y + 30.0)
           << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#111827\">" << html_escape(graph.nodes[i].label)
           << "</text>";
  }
  output << "</svg>";
  return output.str();
}

std::string export_obsidian_markdown(const GraphSnapshot& graph) {
  std::ostringstream output;
  for (const auto& node : graph.nodes) {
    output << "# " << node.label << "\n\n";
    output << "- id: `" << node.id << "`\n";
    output << "- kind: `" << node.kind << "`\n";
    output << "- source: `" << node.source_file << "`\n";
    output << "- links:";
    bool wrote = false;
    for (const auto& edge : graph.edges) {
      if (edge.source == node.id) {
        output << " [[" << edge.target << "]]";
        wrote = true;
      }
    }
    if (!wrote) {
      output << " none";
    }
    output << "\n\n";
  }
  return output.str();
}

std::string export_neo4j_cypher(const GraphSnapshot& graph) {
  std::ostringstream output;
  for (const auto& node : graph.nodes) {
    output << "MERGE (n:Symbol {id: '" << cypher_escape(node.id) << "'}) "
           << "SET n.label = '" << cypher_escape(node.label) << "', n.kind = '" << cypher_escape(node.kind)
           << "', n.source_file = '" << cypher_escape(node.source_file) << "';\n";
  }
  for (const auto& edge : graph.edges) {
    output << "MATCH (a:Symbol {id: '" << cypher_escape(edge.source) << "'}), "
           << "(b:Symbol {id: '" << cypher_escape(edge.target) << "'}) "
           << "MERGE (a)-[:`" << cypher_escape(edge.relation) << "`]->(b);\n";
  }
  return output.str();
}

std::string export_call_flow_html(const GraphSnapshot& graph) {
  std::ostringstream output;
  output << "<!doctype html><html><head><meta charset=\"utf-8\"><title>call flow</title></head><body>";
  output << "<h1>Call Flow</h1><ol>";
  for (const auto& edge : graph.edges) {
    if (edge.relation == "CALLS") {
      output << "<li>" << html_escape(edge.source) << " calls " << html_escape(edge.target) << "</li>";
    }
  }
  output << "</ol></body></html>";
  return output.str();
}

}  // namespace cgraph
