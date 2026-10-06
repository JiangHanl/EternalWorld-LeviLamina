#pragma once
#include <string>
#include <string_view>

namespace eternal::commerce::detail {
// Standard SHA-256 (FIPS 180-4) for migration checksums and business-key digests.
std::string sha256(std::string_view bytes);
} // namespace eternal::commerce::detail
