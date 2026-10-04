#pragma once

#include <cstdint>

namespace eternal::native {
void setLifecycle(std::uint32_t state) noexcept;
std::uint32_t lifecycle() noexcept;
bool apiSelfcheck() noexcept;
}
