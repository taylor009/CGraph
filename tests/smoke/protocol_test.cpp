#include "cgraph/protocol.hpp"

int main() {
  const auto request = cgraph::make_request("status", {{"project_root", "/tmp/project"}});
  if (!cgraph::protocol_version_matches(request)) {
    return 1;
  }

  const auto frame = cgraph::encode_frame(request);
  const auto decoded = cgraph::decode_frame(frame);
  if (!decoded.has_value() || (*decoded)["op"] != "status") {
    return 1;
  }

  auto mismatch = request;
  mismatch["protocol_version"] = cgraph::kProtocolVersion + 1;
  if (cgraph::protocol_version_matches(mismatch)) {
    return 1;
  }

  auto truncated = frame;
  truncated.pop_back();
  if (cgraph::decode_frame(truncated).has_value()) {
    return 1;
  }

  return 0;
}
