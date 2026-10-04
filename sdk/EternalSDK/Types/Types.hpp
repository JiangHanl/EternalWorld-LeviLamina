#pragma once
#include "../Module/module_abi.h"
#include "../Core/core_abi.h"
#include <limits>
#include <string_view>
namespace eternal::sdk {
using MinorUnits = int64_t;
using PlayerUuid = EcUuid;
using Capability = EcCapability;
inline constexpr MinorUnits minorUnitsPerLiang = EC_COIN_MINOR_UNITS_PER_LIANG;
inline EmUtf8View view(std::string_view value) noexcept {
    if(value.size()>std::numeric_limits<uint32_t>::max())return {};
    return {value.data(),static_cast<uint32_t>(value.size()),0};
}
inline std::string_view text(EmUtf8View value) noexcept {
    return value.data ? std::string_view(value.data,value.length) : std::string_view{};
}
template<class T> T output() noexcept {T value{};value.struct_size=sizeof(T);value.struct_version=EM_STRUCT_VERSION;return value;}
} // namespace eternal::sdk
