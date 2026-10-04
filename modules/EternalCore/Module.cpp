#include "EternalSDK/Module/module_abi.h"
#include "EternalSDK/abi.h"
#include "api/ApiService.hpp"

namespace {
constexpr EmUtf8View view(const char* data, uint32_t length) { return {data, length, 0}; }
const EmModuleDescriptor descriptor{
    .struct_size = sizeof(EmModuleDescriptor),
    .struct_version = EM_STRUCT_VERSION,
    .id = view("core", 4),
    .display_name = view("EternalCore", 11),
    .version = {0, 1, 0, 0},
    .abi_major = EM_ABI_MAJOR,
    .abi_minor = EM_ABI_MINOR,
    .required_host_capabilities = EM_HOST_LOGGING | EM_HOST_SERVICES,
    .provided_capabilities = 0,
    .dependencies = nullptr,
    .dependency_count = 0,
    .flags = 0,
};
EmHostContext host{};
bool loaded = false;
bool enabled = false;
}

extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor() noexcept {
    return &descriptor;
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context) noexcept {
    try {
        if (loaded) return EM_CONFLICT;
        if (!context || context->struct_size < sizeof(EmHostContext)
            || context->struct_version != EM_STRUCT_VERSION || context->abi_major != EM_ABI_MAJOR
            || context->abi_minor < EM_ABI_MINOR) return EM_ABI_MISMATCH;
        if ((context->capabilities & descriptor.required_host_capabilities) != descriptor.required_host_capabilities
            || !context->log || !context->publish_service || !context->instance) return EM_UNSUPPORTED;
        eternal::native::setLifecycle(EC_LIFECYCLE_STARTING);
        if (!eternal::native::apiSelfcheck()) return EM_INTERNAL_ERROR;
        host = *context;
        loaded = true;
        host.log(host.instance, EM_LOG_INFO, view("Core API 1.0 loaded; asset capabilities remain disabled", 55));
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept {
    try {
        if (!loaded) return EM_NOT_READY;
        if (enabled) return EM_CONFLICT;
        const auto* api = EternalCore_QueryApi(EC_API_MAJOR, EC_API_MINOR);
        if (!api) return EM_INTERNAL_ERROR;
        const EmServiceOffer offer{
            sizeof(EmServiceOffer), EM_STRUCT_VERSION, view(EM_CORE_SERVICE_ID, sizeof(EM_CORE_SERVICE_ID)-1),
            EC_API_MAJOR, EC_API_MINOR, 0, api, sizeof(EternalCoreApi), 0,
        };
        const auto result = host.publish_service(host.instance, &offer);
        if (result != EM_OK) return result;
        eternal::native::setLifecycle(EC_LIFECYCLE_READY);
        enabled = true;
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept {
    try {
        // Revoke every public method before Host removes this provider's registry entry.
        eternal::native::setLifecycle(EC_LIFECYCLE_DRAINING);
        enabled = false;
        eternal::native::setLifecycle(EC_LIFECYCLE_STOPPED);
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept {
    if (enabled) return EM_CONFLICT;
    loaded = false;
    host = {};
    return EM_OK;
}
