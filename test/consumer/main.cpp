#include <string_view>

#include "pcrepp.hpp"

auto main() -> int {
  auto const ctx = pcrepp::context<>::create(R"((\w+))");
  if (!ctx) {
    return 1;
  }
  auto const result = ctx->find(std::string_view{"hello"});
  if (!result || !*result) {
    return 2;
  }
  return 0;
}

