#include "EternalSDK/Module/module_abi.h"
#include "EternalSDK/abi.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

// Example only: independent source, SDK-only cross-module communication.
namespace {
template<size_t N> constexpr EmUtf8View view(const char (&text)[N]) { return {text, N-1, 0}; }
const EmDependency dependencies[]{
    {sizeof(EmDependency), EM_STRUCT_VERSION, view("core"), {0, 1, 0, 0}, 0, EM_DEPENDENCY_OPTIONAL, 0},
};
const EmModuleDescriptor descriptor{
    .struct_size = sizeof(EmModuleDescriptor),
    .struct_version = EM_STRUCT_VERSION,
    .id = view("example"),
    .display_name = view("EternalExample"),
    .version = {0, 1, 0, 0},
    .abi_major = EM_ABI_MAJOR,
    .abi_minor = EM_ABI_MINOR,
    .required_host_capabilities = EM_HOST_LOGGING | EM_HOST_EVENTS,
    .optional_host_capabilities = EM_HOST_SERVICES,
    .provided_capabilities = 0,
    .dependencies = dependencies,
    .dependency_count = 1,
    .flags = 0,
};
EmHostContext host{};
uint64_t subscription = 0;
bool loaded = false;
bool enabled = false;
bool logEvents = true;

void EM_CALL onPing(void*, const EmEvent*) noexcept {
    try {
        if (enabled && logEvents) host.log(host.instance, EM_LOG_INFO, view("Example event received"));
    } catch (...) { /* No exception crosses the callback boundary. */ }
}
}

extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor() noexcept { return &descriptor; }
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context) noexcept {
    try {
        if (loaded) return EM_CONFLICT;
        if (!context || context->struct_size < sizeof(EmHostContext) || context->struct_version != EM_STRUCT_VERSION
            || context->abi_major != EM_ABI_MAJOR || context->abi_minor < EM_ABI_MINOR) return EM_ABI_MISMATCH;
        if ((context->capabilities & descriptor.required_host_capabilities) != descriptor.required_host_capabilities
            || !context->instance || !context->log || !context->subscribe || !context->unsubscribe) return EM_UNSUPPORTED;
        const auto directory = context->config_directory;
        if (!directory.data || !directory.length || directory.reserved) return EM_INVALID_ARGUMENT;
        const auto configPath = std::filesystem::path(std::u8string(directory.data, directory.data + directory.length)) / "example.json";
        logEvents = true;
        if (std::filesystem::exists(configPath)) {
            if (std::filesystem::file_size(configPath) > 8192) return EM_LIMIT_EXCEEDED;
            std::ifstream input(configPath, std::ios::binary);
            if (!input) return EM_INVALID_ARGUMENT;
            const auto config = nlohmann::json::parse(input);
            if (!config.is_object() || config.size() != 1 || !config.contains("logEvents") || !config["logEvents"].is_boolean())
                return EM_INVALID_ARGUMENT;
            logEvents = config["logEvents"].get<bool>();
        }
        host = *context;
        loaded = true;
        return EM_OK;
    } catch (...) { return EM_INVALID_ARGUMENT; }
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept {
    try {
        if (!loaded) return EM_NOT_READY;
        if (enabled) return EM_CONFLICT;
        EmSubscription request{sizeof(EmSubscription), EM_STRUCT_VERSION, view("example.ping"), onPing, nullptr, 0};
        const auto result = host.subscribe(host.instance, &request, &subscription);
        if (result != EM_OK) return result;
        enabled = true;
        host.log(host.instance, EM_LOG_INFO, view("Example enabled; no business functionality"));
        // A service is optional. Its absence must not break this consumer.
        if ((host.capabilities & EM_HOST_SERVICES) && host.query_service) {
            EmServiceRequest core{sizeof(EmServiceRequest), EM_STRUCT_VERSION, view(EM_CORE_SERVICE_ID), EC_API_MAJOR, EC_API_MINOR, 0, 0};
            EmServiceReference reference{sizeof(EmServiceReference), EM_STRUCT_VERSION};
            if (host.query_service(host.instance, &core, &reference) == EM_OK && reference.table_size >= sizeof(EternalCoreApi)) {
                const auto* api = static_cast<const EternalCoreApi*>(reference.table);
                EcVersionInfo version{sizeof(EcVersionInfo), EC_STRUCT_VERSION};
                if (api && api->struct_size >= sizeof(EternalCoreApi) && api->api_major == EC_API_MAJOR
                    && api->api_minor >= EC_API_MINOR && api->get_version && api->get_version(&version) == EC_OK)
                    host.log(host.instance, EM_LOG_INFO, view("Optional Core public service is available"));
            }
        }
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept {
    enabled = false;
    if (subscription && host.unsubscribe) host.unsubscribe(host.instance, subscription);
    subscription = 0;
    return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept {
    if (enabled) return EM_CONFLICT;
    loaded = false;
    host = {};
    return EM_OK;
}
