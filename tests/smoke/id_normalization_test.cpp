#include "cgraph/normalize.hpp"

#include <string_view>

namespace {

struct Case {
  std::string_view input;
  std::string_view expected;
};

constexpr Case kCases[] = {
    {"Hello, World!", "hello_world"},
    {"  Foo---Bar  ", "foo_bar"},
    {"Crème Brûlée", "crème_brûlée"},
    {"Café", "café"},
    {"東京.Service", "東京_service"},
    {"Привет мир", "привет_мир"},
    {"Class::Method", "class_method"},
    {"naïve_user42", "naïve_user42"},
    {"① Service", "1_service"},
    {"___Already__ID___", "already_id"},
    {"", ""},
};

}  // namespace

int main() {
  for (const auto& test_case : kCases) {
    if (cgraph::make_id(test_case.input) != test_case.expected) {
      return 1;
    }
  }
  return 0;
}
