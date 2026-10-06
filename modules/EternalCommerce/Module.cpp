#include "EternalSDK/Core/Phase2Client.hpp"
#include "domain/Commerce.hpp"
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {
using namespace eternal::sdk;
EcUtf8View ecView(std::string_view text) {
    return {text.data(), static_cast<uint32_t>(text.size()), 0};
}
constexpr uint64_t caps = EC_MODULE_CAP_PLAYER_READ | EC_MODULE_CAP_PERMISSION_CHECK |
                          EC_MODULE_CAP_MONEY | EC_MODULE_CAP_REPUTATION |
                          EC_MODULE_CAP_AUDIT | EC_MODULE_CAP_EVENTS;
const EmDependency dependencies[]{
    {sizeof(EmDependency), EM_STRUCT_VERSION, view("core"), {0, 1, 0, 0}, caps, 0, 0}};
const EmModuleDescriptor descriptor{sizeof(descriptor), EM_STRUCT_VERSION, view("commerce"),
                                    view("EternalCommerce"), {0, 1, 0, 0}, EM_ABI_MAJOR,
                                    EM_ABI_MINOR,
                                    EM_HOST_LOGGING | EM_HOST_SERVICES | EM_HOST_EVENTS, 0, 0,
                                    dependencies, 1, 0, {0, 0}};
EmHostContext host{};
Phase2Client client;
std::unique_ptr<eternal::commerce::Commerce> commerce;
bool loaded{}, enabled{};

EcStatus EM_CALL onTransfer(void *, const EcPhase2Invocation *invocation,
                            EcUtf8Buffer *reply) noexcept {
    if (!invocation || !reply || !reply->data || !reply->capacity)
        return EC_INVALID_ARGUMENT;
    static constexpr char message[] = "Commerce transfer: business features disabled";
    const auto length = static_cast<uint32_t>(sizeof(message));
    if (reply->capacity < length)
        return EC_BUFFER_TOO_SMALL;
    std::memcpy(reply->data, message, length);
    reply->required = length;
    return EC_UNSUPPORTED;
}
} // namespace

extern "C" EM_EXPORT const EmModuleDescriptor *EM_CALL EternalModule_GetDescriptor() noexcept {
    return &descriptor;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext *context) noexcept {
    if (loaded)
        return EM_CONFLICT;
    if (!context || !accepts(*context) || !context->instance || !context->query_service ||
        !context->log)
        return EM_ABI_MISMATCH;
    if ((context->capabilities & descriptor.required_host_capabilities) !=
        descriptor.required_host_capabilities)
        return EM_UNSUPPORTED;
    host = *context;
    loaded = true;
    return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept {
    try {
        if (!loaded)
            return EM_NOT_READY;
        if (enabled)
            return EM_CONFLICT;
        const auto discovered = Phase2Client::discover(host, caps, client);
        if (discovered != EM_OK)
            return discovered;
        auto directory = std::filesystem::u8path(
            std::string(host.data_directory.data, host.data_directory.length));
        std::filesystem::create_directories(directory);
        auto u8path = (directory / "commerce.sqlite3").u8string();
        commerce = std::make_unique<eternal::commerce::Commerce>(
            std::string(reinterpret_cast<const char *>(u8path.data()), u8path.size()));
        EcPhase2CommandRouteRequest route{};
        route.route_id = ecView("transfer");
        route.operation_mask = UINT64_C(1) << (EC_P2_OP_TRANSFER - 1);
        route.callback = onTransfer;
        route.user = nullptr;
        const auto registered = client.registerRoute(route);
        if (registered != EC_OK) {
            commerce.reset();
            client.reset();
            return registered;
        }
        enabled = true;
        host.log(host.instance, EM_LOG_INFO, view("Commerce enabled; transfer route registered"));
        return EM_OK;
    } catch (...) {
        enabled = false;
        commerce.reset();
        client.reset();
        return EM_INTERNAL_ERROR;
    }
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept {
    if (enabled) {
        EcPhase2RouteRemovalRequest removal{};
        removal.route_id = ecView("transfer");
        client.unregisterRoute(removal);
    }
    enabled = false;
    commerce.reset();
    client.reset();
    return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept {
    if (enabled)
        return EM_CONFLICT;
    loaded = false;
    host = {};
    return EM_OK;
}
