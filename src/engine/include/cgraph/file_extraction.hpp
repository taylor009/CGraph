#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/extractor.hpp"

namespace cgraph {

[[nodiscard]] ExtractionResult extract_detected_file(const DetectedFile& file);

}  // namespace cgraph
