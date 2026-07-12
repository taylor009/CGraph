#!/usr/bin/env python3
"""Bootstrap a ground-truth code-retrieval eval dataset by mining a repo's git history.

Reusable across any project that has a CGraph ``graph.json`` export. The harness
depends only on the CGraph graph *format* (node-link JSON with ``source_location``
spans and relation-typed edges), never on a particular repo's contents.

Two tiers of rows are emitted to ``<out>/eval/queries.jsonl``:

  Tier 0 (git-mined, primary): for each non-merge commit, the cleaned commit
    subject becomes the query; nodes whose ``source_location`` intersects the
    commit's changed line ranges are graded 2 (directly changed) and their graph
    neighbours (via configured edge relations) are graded 1.

  Tier 1 (synthetic, --synthetic): a few generated questions per top-centrality
    node, clearly tagged ``fidelity="synthetic"``.

Everything noise-related (commit/file filters, line drift handling, grading,
synthetic knobs) lives in a per-project TOML config so the harness adapts to a
project's language and churn profile. See the shipped defaults in DEFAULT_CONFIG.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
import tomllib
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Iterable

# ---------------------------------------------------------------------------
# Defaults (every value overridable via the per-project TOML config)
# ---------------------------------------------------------------------------

DEFAULT_CONFIG: dict = {
    "mining": {
        "max_commits": 0,        # 0 = all non-merge commits; else most-recent N
        "since": "",             # optional git date filter, e.g. "2024-01-01"
        "skip_merges": True,
        "rebase_lines": True,    # map commit-era changed lines onto HEAD spans
    },
    "noise": {
        "skip_subject_patterns": [
            r"^merge ", r"^bump ", r"^release ", r"wip",
            r"^fixup!", r"^squash!", r"^v?\d+\.\d+\.\d+$",
        ],
        "ignore_path_globs": [
            "**/*.lock", "**/package-lock.json", "**/pnpm-lock.yaml", "**/yarn.lock",
            "**/*.min.js", "**/*.snap", "**/__snapshots__/**",
            "**/dist/**", "**/build/**", "**/node_modules/**", "**/vendor/**",
            "**/*.generated.*", "**/*_pb2.py", "**/*.pb.go",
            "cgraph-out/**", "research/**",
        ],
        "doc_path_globs": ["**/*.md", "docs/**", "openspec/**", "**/*.rst", "**/*.txt"],
        "heuristics": {
            "max_files_per_commit": 12,
            "cap_to_largest_file": True,
            "min_changed_lines": 1,
            "drop_pure_rename": True,
            "drop_whitespace_only": True,
            "drop_import_only": True,
        },
    },
    "grading": {
        "direct_grade": 2,
        "neighbor_grade": 1,
        "adjacency_relations": [
            "CALLS", "references", "REFERENCES", "imports", "IMPLEMENTED_BY", "RELATED_TO",
        ],
        "max_neighbors_per_node": 25,
        "neighbor_directions": "both",   # out | in | both
        # A matched symbol that spans >= this fraction of its file's symbol extent
        # AND encloses another matched symbol is a file-spanning wrapper (namespace,
        # top-level module) rather than a precise hit -> dropped from grade 2.
        "drop_file_spanning_containers": True,
        "container_span_fraction": 0.9,
    },
    "synthetic": {
        "top_centrality_n": 30,
        "questions_per_node": 2,
        "centrality_property": "degree_centrality",
    },
    "query": {
        "strip_conventional_prefix": True,
        "strip_trailing_ticket": True,
    },
    # patterns for "import-only" hunks; a line counts as an import if it matches any.
    "import_line_patterns": [
        r"^\s*import\b", r"^\s*from\s+\S+\s+import\b",     # python / js / ts
        r"^\s*#include\b", r"^\s*using\b", r"^\s*require\(",  # c/c++/js
        r"^\s*use\b",                                       # rust / php
    ],
}

EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904"  # git's canonical empty tree


def deep_merge(base: dict, override: dict) -> dict:
    """Recursively merge ``override`` into a copy of ``base``."""
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def load_config(path: str) -> tuple[dict, str]:
    """Return (config, human-readable-source). Missing file -> built-in defaults."""
    if path and os.path.isfile(path):
        with open(path, "rb") as fh:
            user = tomllib.load(fh)
        return deep_merge(DEFAULT_CONFIG, user), os.path.abspath(path)
    return dict(DEFAULT_CONFIG), "built-in defaults (no config file found)"


# ---------------------------------------------------------------------------
# Git helpers
# ---------------------------------------------------------------------------

def git(root: str, *args: str) -> str:
    res = subprocess.run(
        ["git", "-C", root, *args],
        check=True, capture_output=True, text=True,
    )
    return res.stdout


def list_commits(root: str, cfg: dict) -> list[str]:
    args = ["rev-list", "HEAD"]
    if cfg["mining"]["skip_merges"]:
        args.append("--no-merges")
    if cfg["mining"]["since"]:
        args.append(f"--since={cfg['mining']['since']}")
    shas = [s for s in git(root, *args).splitlines() if s.strip()]
    n = int(cfg["mining"]["max_commits"])
    if n > 0:
        shas = shas[:n]
    return shas


def commit_subject(root: str, sha: str) -> str:
    return git(root, "show", "-s", "--format=%s", sha).strip()


@dataclass
class FileChange:
    path: str            # repo-relative, post-image (new) path
    old_path: str | None
    added: int
    deleted: int
    is_rename: bool
    changed_lines: set[int] = field(default_factory=set)   # new-side line numbers


def parse_numstat(root: str, sha: str) -> list[FileChange]:
    """Per-file add/delete counts and rename detection for one commit."""
    parent = f"{sha}^" if has_parent(root, sha) else EMPTY_TREE
    out = git(root, "diff", "--numstat", "-M", parent, sha)
    changes: list[FileChange] = []
    for line in out.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        added_s, deleted_s, pathspec = parts[0], parts[1], parts[2]
        added = 0 if added_s == "-" else int(added_s)
        deleted = 0 if deleted_s == "-" else int(deleted_s)
        old_path = None
        is_rename = False
        # rename form: "old => new" or "dir/{old => new}/file"
        if " => " in pathspec or "{" in pathspec:
            is_rename = True
            old_path, path = _parse_rename(pathspec)
        else:
            path = pathspec
        changes.append(FileChange(path, old_path, added, deleted, is_rename))
    return changes


def _parse_rename(spec: str) -> tuple[str, str]:
    """Decode git's rename pathspec into (old, new)."""
    m = re.search(r"\{(.*?) => (.*?)\}", spec)
    if m:
        old = spec[:m.start()] + m.group(1) + spec[m.end():]
        new = spec[:m.start()] + m.group(2) + spec[m.end():]
        return re.sub(r"//+", "/", old), re.sub(r"//+", "/", new)
    if " => " in spec:
        old, new = spec.split(" => ", 1)
        return old.strip(), new.strip()
    return spec, spec


_parent_cache: dict[str, bool] = {}


def has_parent(root: str, sha: str) -> bool:
    if sha not in _parent_cache:
        try:
            git(root, "rev-parse", "--verify", "--quiet", f"{sha}^")
            _parent_cache[sha] = True
        except subprocess.CalledProcessError:
            _parent_cache[sha] = False
    return _parent_cache[sha]


HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def changed_new_lines(root: str, sha: str, path: str, whitespace_insensitive: bool = False) -> set[int]:
    """New-side line numbers touched by ``sha`` in ``path`` (added lines + deletion anchors)."""
    parent = f"{sha}^" if has_parent(root, sha) else EMPTY_TREE
    args = ["diff", "--unified=0"]
    if whitespace_insensitive:
        args.append("-w")
    args += [parent, sha, "--", path]
    out = git(root, *args)
    return _added_lines_from_diff(out)


def _added_lines_from_diff(diff_text: str) -> set[int]:
    lines: set[int] = set()
    cur_new = 0
    in_hunk = False
    for raw in diff_text.splitlines():
        if raw.startswith("@@"):
            m = HUNK_RE.match(raw)
            if not m:
                continue
            new_start = int(m.group(3))
            new_count = m.group(4)
            new_count = 1 if new_count is None else int(new_count)
            cur_new = new_start
            in_hunk = True
            if new_count == 0:
                # pure deletion hunk: anchor at the new-side position
                lines.add(max(1, new_start))
            continue
        if not in_hunk or raw.startswith(("---", "+++")):
            continue
        if raw.startswith("+"):
            lines.add(cur_new)
            cur_new += 1
        elif raw.startswith("-"):
            # deletion: anchor at current new position, do not advance
            lines.add(max(1, cur_new))
        elif raw.startswith(" "):
            cur_new += 1
    return lines


def is_whitespace_only(root: str, sha: str, path: str) -> bool:
    """True if every change in ``path`` for ``sha`` is whitespace/formatting-only."""
    if not changed_new_lines(root, sha, path):
        return True
    return not changed_new_lines(root, sha, path, whitespace_insensitive=True)


def build_line_rebaser(root: str, sha: str, path: str):
    """Return a fn mapping ``sha``-era new-side lines to HEAD lines for ``path``.

    Parses ``git diff <sha>..HEAD -- path``: the '-' side is in ``sha`` coordinates,
    the '+' side is HEAD coordinates. Lines outside changed hunks shift by the
    cumulative offset; lines inside a changed hunk map to that hunk's HEAD range.
    """
    try:
        diff = git(root, "diff", "--unified=0", sha, "HEAD", "--", path)
    except subprocess.CalledProcessError:
        return None
    hunks = []  # (old_start, old_count, new_start, new_count)
    for raw in diff.splitlines():
        if raw.startswith("@@"):
            m = HUNK_RE.match(raw)
            if not m:
                continue
            os_, oc = int(m.group(1)), m.group(2)
            ns_, nc = int(m.group(3)), m.group(4)
            oc = 1 if oc is None else int(oc)
            nc = 1 if nc is None else int(nc)
            hunks.append((os_, oc, ns_, nc))
    hunks.sort()

    def rebase(line: int) -> set[int]:
        offset = 0
        for os_, oc, ns_, nc in hunks:
            old_end = os_ + oc - 1
            if oc > 0 and os_ <= line <= old_end:
                # inside a modified region -> map to the HEAD-side range
                if nc == 0:
                    return {max(1, ns_)}
                return set(range(ns_, ns_ + nc))
            if old_end < line:
                offset += (nc - oc)
        return {max(1, line + offset)}

    return rebase


# ---------------------------------------------------------------------------
# Graph model
# ---------------------------------------------------------------------------

FILE_TYPES = {"file"}


@dataclass
class GraphModel:
    nodes: dict                      # id -> node dict
    graph_root: str                  # detected absolute build root
    rel_of_node: dict                # node id -> repo-relative file path
    file_node_by_path: dict          # repo-rel path -> file node id
    doc_node_by_path: dict           # repo-rel path -> document/semantic node id (no line spans)
    symbols_by_path: dict            # repo-rel path -> [(node_id, start, end)]
    adjacency: dict                  # node id -> set(neighbor ids) via configured relations
    symbol_files: set                # repo-rel paths that have >=1 non-file code symbol node

    def file_level_node(self, path: str) -> str | None:
        """Best node to label a whole-file change: a code file node, else a doc node."""
        return self.file_node_by_path.get(path) or self.doc_node_by_path.get(path)

    def all_paths(self) -> set:
        return set(self.file_node_by_path) | set(self.symbols_by_path) | set(self.doc_node_by_path)


def relpath_from_root(graph_root: str, source_file: str) -> str | None:
    try:
        rel = os.path.relpath(source_file, graph_root)
    except ValueError:
        return None
    if rel.startswith(".."):
        return None
    return rel.replace(os.sep, "/")


def load_graph(graph_path: str, cfg: dict) -> GraphModel:
    with open(graph_path, "rb") as fh:
        g = json.load(fh)
    nodes = {n["id"]: n for n in g["nodes"]}
    links = g.get("links", g.get("edges", []))

    source_files = [n["source_file"] for n in nodes.values() if n.get("source_file")]
    if not source_files:
        raise SystemExit("graph.json has no source_file fields; cannot map to repo paths")
    graph_root = os.path.commonpath(source_files)
    if os.path.isfile(graph_root):
        graph_root = os.path.dirname(graph_root)

    rel_of_node: dict = {}
    file_node_by_path: dict = {}
    doc_node_by_path: dict = {}
    symbols_by_path: dict = defaultdict(list)
    symbol_files: set = set()

    for nid, n in nodes.items():
        sf = n.get("source_file")
        if not sf:
            continue
        rel = relpath_from_root(graph_root, sf)
        if rel is None:
            continue
        rel_of_node[nid] = rel
        ntype = n.get("type")
        if ntype in FILE_TYPES:
            file_node_by_path.setdefault(rel, nid)
            continue
        loc = n.get("source_location") or {}
        start = loc.get("start_line")
        end = loc.get("end_line")
        if start is None:
            # no line span (e.g. semantic document/concept nodes) -> file-level label only
            doc_node_by_path.setdefault(rel, nid)
            continue
        if end is None or end < start:
            end = start
        symbols_by_path[rel].append((nid, int(start), int(end)))
        # functions/classes/fields/etc. (non-file, non-import) signal real code extraction
        if ntype not in {"import"}:
            symbol_files.add(rel)

    # adjacency over configured relations
    wanted = set(cfg["grading"]["adjacency_relations"])
    direction = cfg["grading"]["neighbor_directions"]
    adjacency: dict = defaultdict(set)
    for l in links:
        if l.get("relation") not in wanted:
            continue
        s, t = l.get("source"), l.get("target")
        if s is None or t is None:
            continue
        if direction in ("out", "both"):
            adjacency[s].add(t)
        if direction in ("in", "both"):
            adjacency[t].add(s)

    return GraphModel(
        nodes=nodes,
        graph_root=graph_root,
        rel_of_node=rel_of_node,
        file_node_by_path=dict(file_node_by_path),
        doc_node_by_path=doc_node_by_path,
        symbols_by_path={k: v for k, v in symbols_by_path.items()},
        adjacency=adjacency,
        symbol_files=symbol_files,
    )


# ---------------------------------------------------------------------------
# Query cleaning
# ---------------------------------------------------------------------------

CONV_PREFIX_RE = re.compile(r"^(\w+)(\([^)]*\))?(!)?:\s*")
TRAILING_TICKET_RE = re.compile(r"\s*[\(\[]?(#\d+|[A-Z][A-Z0-9]+-\d+)[\)\]]?\s*$")


def clean_subject(subject: str, cfg: dict) -> str:
    q = subject.strip()
    if cfg["query"]["strip_conventional_prefix"]:
        q = CONV_PREFIX_RE.sub("", q, count=1)
    if cfg["query"]["strip_trailing_ticket"]:
        q = TRAILING_TICKET_RE.sub("", q)
    return q.strip()


# ---------------------------------------------------------------------------
# Path filtering
# ---------------------------------------------------------------------------

def matches_any(path: str, globs: Iterable[str]) -> bool:
    return any(fnmatch.fnmatch(path, g) or fnmatch.fnmatch(os.path.basename(path), g)
               for g in globs)


# ---------------------------------------------------------------------------
# Tier 0 mining
# ---------------------------------------------------------------------------

@dataclass
class Skip:
    sha: str
    reason: str


def mine_commits(root: str, gm: GraphModel, cfg: dict, summary: "Summary") -> list[dict]:
    rows: list[dict] = []
    h = cfg["noise"]["heuristics"]
    import_res = [re.compile(p) for p in cfg["import_line_patterns"]]

    for sha in list_commits(root, cfg):
        short = sha[:7]
        subject = commit_subject(root, sha)

        if any(re.search(p, subject, re.IGNORECASE) for p in cfg["noise"]["skip_subject_patterns"]):
            summary.skip(sha, "subject matched skip pattern")
            continue

        changes = parse_numstat(root, sha)
        if not changes:
            summary.skip(sha, "empty commit (no file diff)")
            continue
        # drop ignored paths
        changes = [c for c in changes if not matches_any(c.path, cfg["noise"]["ignore_path_globs"])]
        if not changes:
            summary.skip(sha, "all files ignored by config")
            continue

        if h["drop_pure_rename"] and all(c.is_rename and c.added == 0 and c.deleted == 0 for c in changes):
            summary.skip(sha, "pure rename, no content change")
            continue

        total_churn = sum(c.added + c.deleted for c in changes)
        if total_churn < h["min_changed_lines"]:
            summary.skip(sha, f"churn {total_churn} < min_changed_lines")
            continue

        # cap noisy multi-file commits. Docs are the usual inflation in a mixed
        # code+docs commit, so drop them first; only if code alone still exceeds
        # the cap do we reduce to the single largest-churn code file.
        capped = False
        if h["cap_to_largest_file"] and len(changes) > h["max_files_per_commit"]:
            doc_changes = [c for c in changes if matches_any(c.path, cfg["noise"]["doc_path_globs"])]
            code_changes = [c for c in changes if c not in doc_changes]
            if code_changes and len(code_changes) <= h["max_files_per_commit"]:
                changes = code_changes
            else:
                pool = code_changes or doc_changes
                changes = [max(pool, key=lambda c: c.added + c.deleted)]
            capped = True

        # survival + line ranges
        relevant_files: list[str] = []
        grade_map: dict[str, tuple[int, str]] = {}   # node_id -> (grade, reason)
        any_symbol = False
        any_file_only = False

        for c in changes:
            # file must survive at HEAD to map onto current node spans
            if not file_exists_at_head(root, c.path):
                continue
            new_lines = changed_new_lines(root, sha, c.path)
            if not new_lines:
                continue
            if h["drop_whitespace_only"] and is_whitespace_only(root, sha, c.path):
                continue
            if h["drop_import_only"] and _all_import_lines(root, sha, c.path, import_res):
                continue

            # rebase commit-era lines onto HEAD coordinates
            if cfg["mining"]["rebase_lines"]:
                rebaser = build_line_rebaser(root, sha, c.path)
                if rebaser is not None:
                    mapped: set[int] = set()
                    for ln in new_lines:
                        mapped |= rebaser(ln)
                    new_lines = mapped

            relevant_files.append(c.path)

            is_doc = matches_any(c.path, cfg["noise"]["doc_path_globs"])
            symbols = gm.symbols_by_path.get(c.path, [])
            if (not is_doc) and symbols and c.path in gm.symbol_files:
                hits = [(nid, start, end) for nid, start, end in symbols
                        if _intersects(new_lines, start, end)]
                hits = _drop_file_spanning_containers(hits, symbols, cfg)
                for nid, start, end in hits:
                    grade_map[nid] = (cfg["grading"]["direct_grade"],
                                      f"changed lines intersect span {start}-{end}")
                if hits:
                    any_symbol = True
                else:
                    # file changed only in regions outside any extracted symbol -> file label
                    fid = gm.file_level_node(c.path)
                    if fid:
                        grade_map.setdefault(fid, (cfg["grading"]["direct_grade"],
                                                   "file changed outside any extracted symbol"))
                        any_file_only = True
            else:
                # doc file, or a language CGraph does not symbol-extract -> file-level label
                fid = gm.file_level_node(c.path)
                if fid:
                    reason = ("doc change, file-level label" if is_doc
                              else "file-level change (no symbol extraction)")
                    grade_map.setdefault(fid, (cfg["grading"]["direct_grade"], reason))
                    any_file_only = True

        if not relevant_files:
            summary.skip(sha, "no changed file survives at HEAD")
            continue
        # No graph node for any changed file (unextracted language, or docs in a
        # repo without semantic enrichment) -> degrade to a file-level-only row:
        # the ground truth lives in relevant_files, relevant[] stays empty.
        file_level_only = not grade_map
        if file_level_only:
            any_file_only = True

        # expand to graph-adjacent neighbours (grade 1)
        direct_ids = list(grade_map.keys())
        cap = cfg["grading"]["max_neighbors_per_node"]
        for nid in direct_ids:
            neighbours = sorted(gm.adjacency.get(nid, set()))[:cap]
            for nb in neighbours:
                if nb in grade_map:
                    continue
                if nb not in gm.nodes:
                    continue
                grade_map[nb] = (cfg["grading"]["neighbor_grade"], f"graph neighbour of {nid}")

        granularity = "mixed" if (any_symbol and any_file_only) else ("symbol" if any_symbol else "file")

        relevant = [
            {"node_id": nid, "grade": grade, "reason": reason}
            for nid, (grade, reason) in sorted(grade_map.items(), key=lambda kv: (-kv[1][0], kv[0]))
        ]
        query = clean_subject(subject, cfg)
        if not query:
            summary.skip(sha, "empty query after cleaning")
            continue

        rows.append({
            "id": f"row-{len(rows) + 1:05d}",
            "query": query,
            "raw_subject": subject,
            "query_rewritten": None,
            "query_source": f"commit:{short}",
            "fidelity": "derived-from-commit",
            "granularity": granularity,
            "relevant": relevant,
            "relevant_files": sorted(set(relevant_files)),
        })
        summary.row(relevant, granularity, capped, file_level_only)

    return rows


_head_files_cache: set | None = None


def file_exists_at_head(root: str, path: str) -> bool:
    global _head_files_cache
    if _head_files_cache is None:
        _head_files_cache = set(git(root, "ls-tree", "-r", "--name-only", "HEAD").splitlines())
    return path in _head_files_cache


def _all_import_lines(root: str, sha: str, path: str, import_res: list[re.Pattern]) -> bool:
    """True if every added line in the commit for this file is an import-like line."""
    parent = f"{sha}^" if has_parent(root, sha) else EMPTY_TREE
    out = git(root, "diff", "--unified=0", parent, sha, "--", path)
    added = [raw[1:] for raw in out.splitlines()
             if raw.startswith("+") and not raw.startswith("+++")]
    if not added:
        return False
    return all(any(r.search(line) for r in import_res) for line in added if line.strip())


def _intersects(lines: set[int], start: int, end: int) -> bool:
    lo, hi = (start, end) if start <= end else (end, start)
    return any(lo <= ln <= hi for ln in lines)


def _drop_file_spanning_containers(hits: list[tuple], all_symbols: list[tuple], cfg: dict) -> list[tuple]:
    """Remove matched symbols that are file-spanning wrappers around another match.

    A hit is dropped iff it covers >= ``container_span_fraction`` of the file's
    whole symbol extent AND strictly encloses another (distinct) matched symbol.
    This sheds namespace / top-level-module nodes (which match every change in the
    file) while keeping precise classes/functions/fields.
    """
    if not cfg["grading"]["drop_file_spanning_containers"] or len(hits) < 2:
        return hits
    frac = cfg["grading"]["container_span_fraction"]
    starts = [s for _, s, _ in all_symbols]
    ends = [e for _, _, e in all_symbols]
    extent = max(ends) - min(starts) + 1
    if extent <= 0:
        return hits
    kept = []
    for nid, s, e in hits:
        span = e - s + 1
        encloses_other = any(
            (s <= s2 and e2 <= e) and (s2, e2) != (s, e)
            for _, s2, e2 in hits
        )
        if span / extent >= frac and encloses_other:
            continue
        kept.append((nid, s, e))
    return kept or hits


# ---------------------------------------------------------------------------
# Tier 1 synthetic
# ---------------------------------------------------------------------------

def synthetic_rows(gm: GraphModel, cfg: dict, start_index: int) -> list[dict]:
    prop = cfg["synthetic"]["centrality_property"]

    def centrality(n: dict) -> float:
        try:
            return float(n.get("properties", {}).get(prop, 0.0))
        except (TypeError, ValueError):
            return 0.0

    candidates = [n for n in gm.nodes.values() if n.get("type") not in FILE_TYPES and n.get("label")]
    candidates.sort(key=centrality, reverse=True)
    top = candidates[: cfg["synthetic"]["top_centrality_n"]]

    templates = [
        "Where is {label} defined?",
        "What calls or depends on {label}?",
        "How does {label} work and what is it responsible for?",
    ]
    n_q = cfg["synthetic"]["questions_per_node"]

    rows: list[dict] = []
    idx = start_index
    for n in top:
        nid = n["id"]
        label = n["label"]
        rel = gm.rel_of_node.get(nid)
        neighbours = sorted(gm.adjacency.get(nid, set()))[: cfg["grading"]["max_neighbors_per_node"]]
        relevant = [{"node_id": nid, "grade": cfg["grading"]["direct_grade"], "reason": "synthetic target node"}]
        for nb in neighbours:
            if nb in gm.nodes:
                relevant.append({"node_id": nb, "grade": cfg["grading"]["neighbor_grade"],
                                 "reason": f"graph neighbour of {nid}"})
        for ti in range(min(n_q, len(templates))):
            idx += 1
            rows.append({
                "id": f"row-{idx:05d}",
                "query": templates[ti].format(label=label),
                "raw_subject": None,
                "query_rewritten": None,
                "query_source": f"synthetic:{nid}",
                "fidelity": "synthetic",
                "granularity": "symbol",
                "relevant": relevant,
                "relevant_files": [rel] if rel else [],
            })
    return rows


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

@dataclass
class Summary:
    rows_emitted: int = 0
    grade_counts: dict = field(default_factory=lambda: defaultdict(int))
    granularity_counts: dict = field(default_factory=lambda: defaultdict(int))
    file_level_only_rows: int = 0
    capped_commits: int = 0
    skips: dict = field(default_factory=lambda: defaultdict(int))
    skip_examples: dict = field(default_factory=dict)

    def skip(self, sha: str, reason: str) -> None:
        self.skips[reason] += 1
        self.skip_examples.setdefault(reason, sha[:7])

    def row(self, relevant: list[dict], granularity: str, capped: bool, file_level_only: bool = False) -> None:
        self.rows_emitted += 1
        self.granularity_counts[granularity] += 1
        if capped:
            self.capped_commits += 1
        if file_level_only:
            self.file_level_only_rows += 1
        for r in relevant:
            self.grade_counts[r["grade"]] += 1


def print_summary(summary: Summary, gm: GraphModel, cfg: dict, config_source: str,
                  synthetic_count: int, ext_granularity: dict) -> None:
    line = "=" * 64
    print(f"\n{line}\nBOOTSTRAP EVAL SUMMARY\n{line}")
    print(f"Config loaded        : {config_source}")
    print(f"Graph build root     : {gm.graph_root}")
    print(f"Rebase lines to HEAD : {cfg['mining']['rebase_lines']}")
    print(f"\nTier 0 rows (git)    : {summary.rows_emitted}")
    print(f"Tier 1 rows (synth)  : {synthetic_count}")
    print(f"Total rows           : {summary.rows_emitted + synthetic_count}")

    print("\nGrade distribution (label entries):")
    for grade in sorted(summary.grade_counts, reverse=True):
        print(f"  grade {grade}: {summary.grade_counts[grade]}")

    print("\nTier 0 granularity (per row):")
    for g in ("symbol", "mixed", "file"):
        if summary.granularity_counts.get(g):
            print(f"  {g:6s}: {summary.granularity_counts[g]}")
    if summary.capped_commits:
        print(f"\nCommits capped to largest file: {summary.capped_commits}")
    if summary.file_level_only_rows:
        print(f"Rows with file-level-only labels (no graph node; relevant_files only): "
              f"{summary.file_level_only_rows}")

    print("\nCommits skipped (reason: count [e.g. sha]):")
    if not summary.skips:
        print("  (none)")
    for reason in sorted(summary.skips, key=lambda r: -summary.skips[r]):
        print(f"  {summary.skips[reason]:4d}  {reason}  [{summary.skip_examples[reason]}]")

    print("\nExtraction granularity achieved, by file extension:")
    for ext in sorted(ext_granularity):
        sym, total = ext_granularity[ext]
        kind = "symbol" if sym == total else ("file-only" if sym == 0 else "partial")
        print(f"  {ext or '(none)':10s}: {sym}/{total} files symbol-extracted -> {kind}")
    file_only_exts = [e for e, (s, t) in ext_granularity.items() if s == 0]
    if file_only_exts:
        print("\n  NOTE: these extensions are NOT symbol-extracted by CGraph "
              "(regex/unhandled);")
        print("        their rows use FILE-LEVEL labels only: "
              + ", ".join(sorted(e or "(none)" for e in file_only_exts)))
    print(line)


def extension_granularity(gm: GraphModel) -> dict:
    """ext -> (num files with symbols, total files in graph)."""
    out: dict = defaultdict(lambda: [0, 0])
    for rel in gm.all_paths():
        ext = os.path.splitext(rel)[1]
        out[ext][1] += 1
        if rel in gm.symbol_files:
            out[ext][0] += 1
    return {k: tuple(v) for k, v in out.items()}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Bootstrap a CGraph code-retrieval eval dataset from git history.")
    ap.add_argument("--root", default=os.getcwd(), help="repo to mine (default: cwd)")
    ap.add_argument("--graph", default=None, help="path to graph.json (default: <root>/cgraph-out/graph.json)")
    ap.add_argument("--out", default=None, help="output dir (default: <root>/research)")
    ap.add_argument("--config", default=None, help="per-project config (default: <root>/.research-eval.toml)")
    ap.add_argument("--synthetic", action="store_true", help="also emit Tier 1 synthetic rows")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    graph_path = args.graph or os.path.join(root, "cgraph-out", "graph.json")
    out_dir = args.out or os.path.join(root, "research")
    config_path = args.config or os.path.join(root, ".research-eval.toml")

    if not os.path.isfile(graph_path):
        print(f"error: graph.json not found at {graph_path}", file=sys.stderr)
        print("       build one with:  cgraph --root <repo> --out cgraph-out", file=sys.stderr)
        return 2

    cfg, config_source = load_config(config_path)
    print(f"Loaded config: {config_source}")

    gm = load_graph(graph_path, cfg)
    summary = Summary()
    rows = mine_commits(root, gm, cfg, summary)

    synthetic_count = 0
    if args.synthetic:
        srows = synthetic_rows(gm, cfg, start_index=len(rows))
        rows.extend(srows)
        synthetic_count = len(srows)

    eval_dir = os.path.join(out_dir, "eval")
    os.makedirs(eval_dir, exist_ok=True)
    out_path = os.path.join(eval_dir, "queries.jsonl")
    with open(out_path, "w") as fh:
        for row in rows:
            fh.write(json.dumps(row) + "\n")

    ext_gran = extension_granularity(gm)
    print_summary(summary, gm, cfg, config_source, synthetic_count, ext_gran)
    print(f"\nWrote {len(rows)} rows -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
