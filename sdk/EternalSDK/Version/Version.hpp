#pragma once
#include "../Module/module_abi.h"
#include "../Core/core_abi.h"
namespace eternal::sdk {
inline constexpr uint32_t moduleAbiMajor=EM_ABI_MAJOR,moduleAbiMinor=EM_ABI_MINOR;
inline constexpr uint32_t coreApiMajor=EC_API_MAJOR,coreApiMinor=EC_API_MINOR;
inline constexpr EmSemVer sdkVersion{0,1,0,0};
inline constexpr bool accepts(const EmHostContext& c) noexcept {
    return c.struct_size==sizeof(c)&&c.struct_version==EM_STRUCT_VERSION&&
           c.abi_major==moduleAbiMajor&&c.abi_minor>=moduleAbiMinor;
}
} // namespace eternal::sdk
