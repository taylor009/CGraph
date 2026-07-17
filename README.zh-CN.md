<div align="center">

<img src="assets/hero.svg" alt="CGraph — 让你的代码库成为可查询的知识图谱" width="100%">

[English](README.md) · **简体中文**

[![License: MIT](https://img.shields.io/badge/License-MIT-6ea8fe?style=flat-square)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![CMake](https://img.shields.io/badge/CMake-vcpkg-064F8C?style=flat-square&logo=cmake&logoColor=white)](CMakePresets.json)
[![MCP](https://img.shields.io/badge/MCP-2024--11--05-59d499?style=flat-square)](https://modelcontextprotocol.io)
[![PRs welcome](https://img.shields.io/badge/PRs-欢迎贡献-f5c451?style=flat-square)](#贡献)

*扫描项目 · 将代码结构提取为确定性图谱 · 通过守护进程、瘦客户端或 MCP 服务器在 ~10 毫秒内查询。*

</div>

---

## 为什么用 CGraph？

要回答**“谁调用了这个？”**或**“改动它会影响什么？”**，通常得翻阅几十个文件——而答案并不在任何单个文件里，它藏在文件*之间的关系*中。CGraph 预先算好这些关系，让你（和你的 AI 代理）用**图谱导航，而非盲目 grep**。

| | |
| --- | --- |
| 🔗 **反向依赖与影响范围** | `graph_impact` 在 ~10 毫秒内返回某符号的所有传递依赖者。在一个真实的 10,706 节点仓库中，它瞬间找出某个“上帝文件”的 **93 个依赖者**——这是 grep 给不了的答案。 |
| 🧭 **用图谱导航，不再 grep** | 按中心度排序的搜索、节点邻域、最短路径，以及**受 token 预算约束的源码打包**——warm 状态下 ~10 毫秒返回，省去大量消耗上下文的文件读取。 |
| 🏛️ **一眼看清架构** | 中心度排序凸显承载全局的关键文件；Leiden/Louvain 社区检测自动将仓库聚成模块。 |
| ⚡ **确定性且快速** | 完整的 **1 万节点图谱约 2 秒**构建完成；提取路径中没有任何 LLM——相同输入，永远得到相同图谱。 |
| 🤖 **为编码代理而生** | 标准 **MCP** 服务器将图谱直接接入 Claude / Codex，让它们基于结构推理，而不是盲目读文件。 |

> **一句话概括：** 对于大到无法装进脑子的代码库，CGraph 就是那个帮你决定*该读哪段代码*的索引——预先算好、按中心度排序、并通过 MCP 暴露的 `ctags`／“查找所有引用”。

## 架构

<div align="center"><img src="assets/architecture.svg" alt="CGraph 架构" width="100%"></div>

- **`cgraph`** —— 一次性扫描 → 可移植的磁盘导出（`graph.json`、`graph.html`、`graph.svg`、`obsidian.md`、`cypher.txt`、`call-flow.html`）。
- **`graphd` + `cgraph-client`** —— 常驻的按项目守护进程，实时监听文件变化；warm 状态下 `query`／`path`／`explain`／`impact`／`context` 均为 ~10 毫秒。
- **`cgraph-mcp`** —— Model Context Protocol 服务器，让代理直接在图谱上导航。

引擎负责确定性提取、片段校验、缓存状态与本地图谱变更；宿主负责模型选择与语义增强。

## 产出效果

一次扫描即可将源码树变成可交互、可探索的图谱——社区着色，枢纽节点按中心度调整大小：

<div align="center"><img src="assets/graph-example.png" alt="CGraph 在 10,708 节点代码库上的交互视图" width="90%"></div>

<sub>真实的 `graph.html` 视图，作用于一个 10,708 节点／28,945 条边的代码库——自包含 HTML，无任何外部 JS。</sub>

## 性能

交互视图过去每次打开都在浏览器里跑一遍 O(N²) 力导向模拟。现在布局**在 C++ 侧用 igraph 预先算好一次**，重绘也做了视口裁剪——两者都**零依赖**：

<div align="center"><img src="assets/performance.svg" alt="稳定耗时：35 秒 → 0 毫秒" width="100%"></div>

| 图规模 | **优化前**稳定耗时 | **优化后** | 单帧重绘 |
| --- | --- | --- | --- |
| 10,000 节点 | **35.2 秒**（主线程卡死） | **~0 毫秒**（静态布局） | **降低 20–34 倍**（缩放/平移） |

## 支持的语言

基于 tree-sitter 的结构提取，另有若干配置格式的正则/结构化提取：

<p>
<img src="https://img.shields.io/badge/C-A8B9CC?style=for-the-badge&logo=c&logoColor=black" alt="C">
<img src="https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++">
<img src="https://img.shields.io/badge/Go-00ADD8?style=for-the-badge&logo=go&logoColor=white" alt="Go">
<img src="https://img.shields.io/badge/Groovy-4298B8?style=for-the-badge&logo=apachegroovy&logoColor=white" alt="Groovy">
<img src="https://img.shields.io/badge/Java-007396?style=for-the-badge&logo=openjdk&logoColor=white" alt="Java">
<img src="https://img.shields.io/badge/JavaScript-F7DF1E?style=for-the-badge&logo=javascript&logoColor=black" alt="JavaScript">
<img src="https://img.shields.io/badge/Kotlin-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white" alt="Kotlin">
<img src="https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white" alt="Python">
<img src="https://img.shields.io/badge/Ruby-CC342D?style=for-the-badge&logo=ruby&logoColor=white" alt="Ruby">
<img src="https://img.shields.io/badge/Scala-DC322F?style=for-the-badge&logo=scala&logoColor=white" alt="Scala">
<img src="https://img.shields.io/badge/TypeScript-3178C6?style=for-the-badge&logo=typescript&logoColor=white" alt="TypeScript">
<img src="https://img.shields.io/badge/TSX-61DAFB?style=for-the-badge&logo=react&logoColor=black" alt="TSX">
</p>

另外还支持对 Apex、Delphi 表单/源码、MSBuild/XML 工程文件以及 MCP 配置文件的结构化/正则提取。

## 快速开始

**前置条件：** CMake 3.25+、Ninja、C++20 编译器、Git、Fortran 编译器（`gfortran`——igraph 会引入 `lapack-reference`），以及 vcpkg。完整步骤见 [安装与配置](#安装与配置)。

```sh
git clone --recurse-submodules https://github.com/taylor009/CGraph.git && cd CGraph
git clone https://github.com/microsoft/vcpkg .vcpkg && ./.vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$PWD/.vcpkg"
cmake --preset default && cmake --build --preset default

# 为本仓库构建图谱，然后打开交互视图
build/default/src/cli/cgraph --root . --out cgraph-out
open cgraph-out/graph.html
```

## 与编码代理配合使用

`cgraph-mcp` 是基于 stdio 的标准 [MCP](https://modelcontextprotocol.io) 服务器（协议 `2024-11-05`）。注册一次，代理即可通过快速图谱查询导航代码库，而非盲目 grep/读文件：

| 工具 | 用途 |
| --- | --- |
| `graph_query` | 按文本搜索节点；按中心度排序 |
| `graph_explain` | 某节点的邻域（调用者、被调用者、导入） |
| `graph_impact` | 改动某节点的传递影响范围 |
| `graph_path` | 两个节点之间的最短路径 |
| `graph_context` | 受 token 预算约束的源码打包（支持自适应聚合） |
| `graph_update` | 内容校验式同步；返回 `content_root` 用于锁定后续读取 |
| `graph_status` | 守护进程、图谱与增强状态 |
| `graph_remember` / `graph_recall` | 会话记忆——`/compact` 前存档，之后恢复 |

**Claude Code**（借助 `CLAUDE_PROJECT_DIR`，一次注册即可跨所有项目使用）：

```sh
claude mcp add --scope user --transport stdio cgraph \
  -- /abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp \
     --daemon /abs/path/to/CGraph/build/default/src/daemon/graphd
```

本仓库还在 `integrations/skills/` 下提供了宿主技能——`cgraph`（遇到结构性问题优先用图谱工具）与 `cgraph-enrich`（语义增强流程）。用 `cgraph skills install` 安装。

## 安装与配置

> **状态：** 早期原生实现。完整命令面（CLI、守护进程、瘦客户端、MCP 服务器）均已具备并经过测试；尚无打包发布——需用 CMake + vcpkg 从源码构建，并从构建目录运行二进制（或将其软链接到 `PATH`）。

<details>
<summary><strong>完整构建步骤、PATH 配置与 sanitizer/fuzzer 预设</strong></summary>

### 1. 克隆（含子模块）

```sh
git clone --recurse-submodules https://github.com/taylor009/CGraph.git && cd CGraph
# 已经克隆但没带子模块？
git submodule update --init --recursive
```

### 2. 让 CMake 指向 vcpkg

```sh
git clone https://github.com/microsoft/vcpkg .vcpkg   # 完整深度——浅克隆会缺失锁定的 baseline
./.vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$PWD/.vcpkg"
```

依赖 `curl`、`igraph`、`nlohmann-json`、`utf8proc` 在 `vcpkg.json` 中声明，首次配置时构建。Linux 上请先安装 Fortran 编译器（`sudo apt-get install -y gfortran`）——igraph 的 `lapack-reference` 需要它。

### 3. 配置、构建、验证

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
build/default/src/cli/cgraph --root . --out cgraph-out
```

二进制位于 `build/default/src/{cli/cgraph, daemon/graphd, client/cgraph-client, mcp/cgraph-mcp}`。

### 4.（可选）将二进制加入 PATH

```sh
mkdir -p ~/.local/bin
for b in cli/cgraph daemon/graphd client/cgraph-client mcp/cgraph-mcp; do
  ln -sf "$PWD/build/default/src/$b" ~/.local/bin/
done
```

### 开发构建

```sh
cmake --preset sanitizers && cmake --build --preset sanitizers && ctest --preset sanitizers  # ASan/UBSan
cmake --preset fuzzers    && cmake --build --preset fuzzers    && ctest --preset fuzzers      # libFuzzer
```

</details>

## 贡献

欢迎提交 Issue 与 PR。请使用 `default` 预设构建，保持 `ctest --preset default` 通过，迭代时优先用 `sanitizers` 预设。交互视图刻意保持**零依赖**——生成的 HTML 中不引入任何 JS 库。

## 许可证

[MIT](LICENSE)。
