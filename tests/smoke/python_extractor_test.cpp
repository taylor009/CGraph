#include "cgraph/python_extractor.hpp"

int main() {
  constexpr auto source = R"py(
import os
from pathlib import Path

class Worker:
    def run(self):
        return Path.cwd()
)py";

  const auto result = cgraph::extract_python(
      cgraph::ExtractionContext{.source_file = "worker.py", .source = source});

  if (result.fragment.nodes.size() < 4) {
    return 1;
  }
  if (result.raw_calls.empty()) {
    return 1;
  }

  bool saw_import = false;
  for (const auto& node : result.fragment.nodes) {
    if (node.kind == "import") {
      saw_import = true;
    }
  }
  if (!saw_import) {
    return 1;
  }

  return 0;
}
