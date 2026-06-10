#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/extractor.hpp"

#include <span>
#include <vector>

namespace cgraph {

[[nodiscard]] ExtractionResult extract_detected_file(const DetectedFile& file);

// Extracts every file concurrently across a bounded worker pool and returns the
// results in input order. The sequence is identical to calling
// extract_detected_file on each file serially; only the wall time differs.
[[nodiscard]] std::vector<ExtractionResult> extract_files(std::span<const DetectedFile> files);

}  // namespace cgraph
