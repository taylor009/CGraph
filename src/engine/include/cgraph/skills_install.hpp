#pragma once

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace cgraph {

// The host-side skills this repo ships. Canonical copies live under
// <repo>/integrations/skills/<name>; install symlinks them into host skill
// directories so any session (in any repo) can discover them.
inline constexpr std::array<std::string_view, 2> kHostSkillNames{"cgraph", "cgraph-enrich"};

struct SkillsConfig {
  std::filesystem::path skills_source_dir;             // <repo>/integrations/skills
  std::vector<std::filesystem::path> host_skill_dirs;  // e.g. ~/.claude/skills, ~/.agents/skills
};

enum class SkillLinkState {
  kOk,           // symlink present and resolves to the expected source dir
  kMissing,      // nothing at the link path
  kWrongTarget,  // symlink present but points elsewhere
  kNotSymlink,   // a real file/dir occupies the link path (never touched)
};

struct SkillLinkStatus {
  std::filesystem::path link;             // <host_skill_dir>/<skill name>
  std::filesystem::path expected_target;  // <skills_source_dir>/<skill name>
  std::filesystem::path actual_target;    // read_symlink() result when a symlink exists
  SkillLinkState state = SkillLinkState::kMissing;
};

// Host skill directories on this machine: $HOME/.claude/skills (what Claude Code
// reads) and $HOME/.agents/skills (the shared agents-cli source of truth). Only
// directories whose parent exists are returned, so a machine without one host
// simply gets the other.
[[nodiscard]] std::vector<std::filesystem::path> default_host_skill_dirs();

// Find the repo's integrations/skills dir by walking up from the executable and
// then from cwd until an ancestor contains integrations/skills/cgraph-enrich/SKILL.md.
// Empty path if neither walk finds it (caller should require --source).
[[nodiscard]] std::filesystem::path resolve_skills_source(const std::filesystem::path& executable,
                                                          const std::filesystem::path& cwd);

// One row per (host dir x skill name), in config order. Pure inspection.
[[nodiscard]] std::vector<SkillLinkStatus> skills_status(const SkillsConfig& config);

// Create the missing symlinks (parent host dir is created if absent). Idempotent:
// kOk rows are left alone. Never clobbers: kNotSymlink and kWrongTarget rows are
// failures, not overwrites. Returns true when every row ends kOk.
[[nodiscard]] bool skills_install(const SkillsConfig& config);

// Remove only symlinks whose target lies inside skills_source_dir. Foreign links
// and real dirs are left untouched. Returns true when nothing owned remains.
[[nodiscard]] bool skills_uninstall(const SkillsConfig& config);

}  // namespace cgraph
