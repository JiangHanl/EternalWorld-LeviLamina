#include "EternalSDK/Module/module_abi.h"
#include "EternalSDK/abi.h"
#include "api/ApiService.hpp"
#include "runtime/Runtime.hpp"
#include <memory>

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
    .required_host_capabilities = EM_HOST_LOGGING | EM_HOST_SERVICES | EM_HOST_EVENTS,
    .provided_capabilities = EC_MODULE_CAP_ALL,
    .dependencies = nullptr,
    .dependency_count = 0,
    .flags = 0,
};
EmHostContext host{};
bool loaded = false;
bool enabled = false;
std::unique_ptr<eternal::core::runtime::Runtime> runtime;
std::filesystem::path path(EmUtf8View value) {
    return std::filesystem::u8path(std::string(value.data, value.length));
}
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
            || !context->log || !context->publish_service || !context->publish_event || !context->instance
            || !context->config_directory.data || !context->data_directory.data) return EM_UNSUPPORTED;
        eternal::native::setLifecycle(EC_LIFECYCLE_STARTING);
        if (!eternal::native::apiSelfcheck()) return EM_INTERNAL_ERROR;
        host = *context;
        runtime = std::make_unique<eternal::core::runtime::Runtime>(path(host.config_directory), path(host.data_directory));
        if (!runtime->load()) {
            const auto& error = runtime->error();
            host.log(host.instance, EM_LOG_ERROR, view(error.data(), static_cast<uint32_t>(error.size())));
            runtime.reset(); host = {}; return EM_INTERNAL_ERROR;
        }
        loaded = true;
        host.log(host.instance, EM_LOG_INFO, view("Core API 1.0 loaded; asset capabilities remain disabled", 55));
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept {
    try {
        if (!loaded) return EM_NOT_READY;
        if (enabled) return EM_CONFLICT;
        if (!runtime || runtime->enable(host) != EM_OK) return EM_INTERNAL_ERROR;
        const auto* api = EternalCore_QueryApi(EC_API_MAJOR, EC_API_MINOR);
        if (!api) return EM_INTERNAL_ERROR;
        const EmServiceOffer offer{
            sizeof(EmServiceOffer), EM_STRUCT_VERSION, view(EM_CORE_SERVICE_ID, sizeof(EM_CORE_SERVICE_ID)-1),
            EC_API_MAJOR, EC_API_MINOR, 0, api, sizeof(EternalCoreApi), 0,
        };
        const auto result = host.publish_service(host.instance, &offer);
        if (result != EM_OK) { runtime->disable(); return result; }
        const EmServiceOffer phase2{
            sizeof(EmServiceOffer), EM_STRUCT_VERSION, view(EC_PHASE2_SERVICE_ID, sizeof(EC_PHASE2_SERVICE_ID)-1),
            EC_PHASE2_API_MAJOR, EC_PHASE2_API_MINOR, EC_MODULE_CAP_ALL, runtime->phase2Api(), sizeof(EternalCorePhase2Api), 0,
        };
        const auto publicResult = host.publish_service(host.instance, &phase2);
        if (publicResult != EM_OK) { runtime->disable(); return publicResult; }
        const EmServiceOffer ingress{
            sizeof(EmServiceOffer), EM_STRUCT_VERSION, view(EC_NATIVE_INGRESS_SERVICE_ID, sizeof(EC_NATIVE_INGRESS_SERVICE_ID)-1),
            EC_NATIVE_INGRESS_MAJOR, EC_NATIVE_INGRESS_MINOR, 0, runtime->nativeApi(), sizeof(EcNativeIngressApi), 0,
        };
        const auto nativeResult = host.publish_service(host.instance, &ingress);
        if (nativeResult != EM_OK) { runtime->disable(); return nativeResult; }
        eternal::native::setLifecycle(EC_LIFECYCLE_READY);
        enabled = true;
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept {
    try {
        // Revoke every public method before Host removes this provider's registry entry.
        eternal::native::setLifecycle(EC_LIFECYCLE_DRAINING);
        if (runtime) runtime->disable();
        enabled = false;
        eternal::native::setLifecycle(EC_LIFECYCLE_STOPPED);
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}

extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept {
    if (enabled) return EM_CONFLICT;
    loaded = false;
    runtime.reset();
    host = {};
    return EM_OK;
}
