#pragma once
#include <string>
#include <string_view>

namespace eternal::core::detail {
std::string sha256(std::string_view bytes);
}
