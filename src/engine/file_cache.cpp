#include "cgraph/file_cache.hpp"
#include "cgraph/content_root.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cgraph {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

[[nodiscard]] std::array<std::uint8_t, 32> sha256_digest(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> message = bytes;
  const auto bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
  message.push_back(0x80U);
  while ((message.size() % 64U) != 56U) {
    message.push_back(0U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8> hash = {
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  };

  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16U; ++index) {
      const auto offset = chunk + (index * 4U);
      words[index] =
          (static_cast<std::uint32_t>(message[offset]) << 24U) |
          (static_cast<std::uint32_t>(message[offset + 1U]) << 16U) |
          (static_cast<std::uint32_t>(message[offset + 2U]) << 8U) |
          static_cast<std::uint32_t>(message[offset + 3U]);
    }
    for (std::size_t index = 16U; index < 64U; ++index) {
      const auto s0 = rotate_right(words[index - 15U], 7) ^ rotate_right(words[index - 15U], 18) ^ (words[index - 15U] >> 3U);
      const auto s1 = rotate_right(words[index - 2U], 17) ^ rotate_right(words[index - 2U], 19) ^ (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];

    for (std::size_t index = 0; index < 64U; ++index) {
      const auto s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto temp1 = h + s1 + choose + kSha256RoundConstants[index] + words[index];
      const auto s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::array<std::uint8_t, 32> result{};
  for (int i = 0; i < 8; ++i) {
    result[static_cast<std::size_t>(i * 4)] = static_cast<std::uint8_t>((hash[static_cast<std::size_t>(i)] >> 24U) & 0xffU);
    result[static_cast<std::size_t>(i * 4 + 1)] = static_cast<std::uint8_t>((hash[static_cast<std::size_t>(i)] >> 16U) & 0xffU);
    result[static_cast<std::size_t>(i * 4 + 2)] = static_cast<std::uint8_t>((hash[static_cast<std::size_t>(i)] >> 8U) & 0xffU);
    result[static_cast<std::size_t>(i * 4 + 3)] = static_cast<std::uint8_t>(hash[static_cast<std::size_t>(i)] & 0xffU);
  }
  return result;
}

[[nodiscard]] std::string digest_to_hex(const std::array<std::uint8_t, 32>& digest) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    output << std::setw(2) << static_cast<int>(byte);
  }
  return output.str();
}

[[nodiscard]] std::string sha256_bytes(const std::vector<std::uint8_t>& bytes) {
  return digest_to_hex(sha256_digest(bytes));
}

[[nodiscard]] std::optional<std::array<std::uint8_t, 32>> hex_to_bytes32(const std::string& hex) {
  if (hex.size() != 64) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 32> bytes{};
  auto nibble = [](char c) -> std::optional<std::uint8_t> {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
    return std::nullopt;
  };
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto high = nibble(hex[i * 2]);
    const auto low = nibble(hex[i * 2 + 1]);
    if (!high || !low) {
      return std::nullopt;
    }
    bytes[i] = static_cast<std::uint8_t>((*high << 4U) | *low);
  }
  return bytes;
}

[[nodiscard]] bool is_project_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  return std::ranges::none_of(path, [](const auto& component) { return component == ".."; });
}

[[nodiscard]] bool same_stat(const FileCacheEntry& lhs, const FileCacheEntry& rhs) {
  return lhs.size == rhs.size && lhs.modified_at == rhs.modified_at;
}

}  // namespace

std::string sha256_hex(std::string_view value) {
  return sha256_bytes(std::vector<std::uint8_t>(value.begin(), value.end()));
}

std::string sha256_file_hex(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<std::uint8_t> bytes;
  char buffer[4096];
  while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
    const auto count = input.gcount();
    bytes.insert(bytes.end(), buffer, buffer + count);
  }
  return sha256_bytes(bytes);
}

FileCacheEntry read_file_cache_entry(const std::filesystem::path& path) {
  std::error_code error;
  FileCacheEntry entry;
  entry.path = path;
  entry.size = std::filesystem::file_size(path, error);
  if (error) {
    entry.size = 0;
    error.clear();
  }
  entry.modified_at = std::filesystem::last_write_time(path, error);
  if (error) {
    entry.modified_at = {};
  }
  entry.sha256 = sha256_file_hex(path);
  return entry;
}

FileCacheClassification classify_cached_file(
    const std::filesystem::path& path,
    std::optional<FileCacheEntry> previous,
    CacheValidation mode) {
  if (!std::filesystem::exists(path)) {
    return FileCacheClassification{.state = CacheState::Deleted};
  }

  FileCacheEntry current;
  current.path = path;
  std::error_code error;
  current.size = std::filesystem::file_size(path, error);
  if (error) {
    return FileCacheClassification{.state = CacheState::Deleted};
  }
  current.modified_at = std::filesystem::last_write_time(path, error);
  if (error) {
    return FileCacheClassification{.state = CacheState::Deleted};
  }

  if (mode == CacheValidation::Metadata && previous.has_value() && same_stat(current, *previous)) {
    current.sha256 = previous->sha256;
    return FileCacheClassification{.state = CacheState::StatHit, .current = std::move(current)};
  }

  current.sha256 = sha256_file_hex(path);
  if (!previous.has_value()) {
    return FileCacheClassification{.state = CacheState::New, .hash_computed = true, .current = std::move(current)};
  }
  if (current.sha256 == previous->sha256) {
    return FileCacheClassification{.state = CacheState::HashHit, .hash_computed = true, .current = std::move(current)};
  }
  return FileCacheClassification{.state = CacheState::Stale, .hash_computed = true, .current = std::move(current)};
}

ContentRoot compute_content_root(
    const std::filesystem::path& project_root,
    std::span<const FileCacheEntry> entries) {
  if (entries.empty()) {
    const std::vector<std::uint8_t> empty_domain = {0x02};
    return ContentRoot{
        .algorithm = std::string{kContentRootAlgorithm},
        .sha256 = digest_to_hex(sha256_digest(empty_domain)),
        .leaf_count = 0,
    };
  }

  struct LeafInput {
    std::string relative_path;
    std::string file_sha256;
  };
  std::vector<LeafInput> leaves;
  leaves.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto relative_path = entry.path.lexically_normal().lexically_relative(project_root.lexically_normal());
    if (!is_project_relative_path(relative_path)) {
      throw std::invalid_argument("content-root leaf is outside the project root");
    }
    leaves.push_back({
        .relative_path = relative_path.generic_string(),
        .file_sha256 = entry.sha256,
    });
  }
  std::ranges::sort(leaves, [](const LeafInput& a, const LeafInput& b) {
    return a.relative_path < b.relative_path;
  });

  using Digest = std::array<std::uint8_t, 32>;
  std::vector<Digest> level;
  level.reserve(leaves.size());
  for (const auto& leaf : leaves) {
    const auto file_hash = hex_to_bytes32(leaf.file_sha256);
    if (!file_hash) {
      throw std::invalid_argument("content-root leaf has an invalid SHA-256 digest");
    }
    const auto path_len = static_cast<std::uint32_t>(leaf.relative_path.size());

    std::vector<std::uint8_t> preimage;
    preimage.reserve(1 + 4 + leaf.relative_path.size() + 32);
    preimage.push_back(0x00);
    preimage.push_back(static_cast<std::uint8_t>((path_len >> 24U) & 0xffU));
    preimage.push_back(static_cast<std::uint8_t>((path_len >> 16U) & 0xffU));
    preimage.push_back(static_cast<std::uint8_t>((path_len >> 8U) & 0xffU));
    preimage.push_back(static_cast<std::uint8_t>(path_len & 0xffU));
    preimage.insert(preimage.end(), leaf.relative_path.begin(), leaf.relative_path.end());
    preimage.insert(preimage.end(), file_hash->begin(), file_hash->end());

    level.push_back(sha256_digest(preimage));
  }

  while (level.size() > 1) {
    std::vector<Digest> next;
    next.reserve((level.size() + 1) / 2);
    for (std::size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        std::vector<std::uint8_t> preimage;
        preimage.reserve(1 + 64);
        preimage.push_back(0x01);
        preimage.insert(preimage.end(), level[i].begin(), level[i].end());
        preimage.insert(preimage.end(), level[i + 1].begin(), level[i + 1].end());
        next.push_back(sha256_digest(preimage));
      } else {
        next.push_back(level[i]);
      }
    }
    level = std::move(next);
  }

  return ContentRoot{
      .algorithm = std::string{kContentRootAlgorithm},
      .sha256 = digest_to_hex(level[0]),
      .leaf_count = leaves.size(),
  };
}

bool is_valid_content_root(const ContentRoot& root) {
  return root.algorithm == kContentRootAlgorithm && hex_to_bytes32(root.sha256).has_value();
}

}  // namespace cgraph
