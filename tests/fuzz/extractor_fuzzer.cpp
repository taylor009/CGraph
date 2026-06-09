#include "cgraph/configured_extractors.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef CGRAPH_FUZZ_LANGUAGE
#error "CGRAPH_FUZZ_LANGUAGE must be defined"
#endif

#ifndef CGRAPH_FUZZ_SOURCE_FILE
#error "CGRAPH_FUZZ_SOURCE_FILE must be defined"
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t max_input_size = 256 * 1024;
  if (size > max_input_size) {
    return 0;
  }

  const std::string source(reinterpret_cast<const char*>(data), size);
  const cgraph::ExtractionContext context{
      .source_file = CGRAPH_FUZZ_SOURCE_FILE,
      .source = source,
  };

  (void)cgraph::extract_configured_language(CGRAPH_FUZZ_LANGUAGE, context);
  return 0;
}
