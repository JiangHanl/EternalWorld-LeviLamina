#include "EternalSDK/Core/Phase2Client.hpp"
#include <array>
#include <charconv>
#include <cstring>
#include <string>

// Test fixture only. It is never included in the production Eternal package.
namespace {
template <std::size_t N> constexpr EmUtf8View view(const char (&value)[N]) {
    return {value, N - 1, 0};
}
const EmDependency dependencies[]{
    {sizeof(EmDependency), EM_STRUCT_VERSION, view("core"), {0, 1, 0, 0},
     EC_MODULE_CAP_ALL, 0, 0},
};
const EmModuleDescriptor descriptor{
    .struct_size = sizeof(EmModuleDescriptor),
    .struct_version = EM_STRUCT_VERSION,
    .id = view("core-validation"),
    .display_name = view("CoreValidationModule"),
    .version = {0, 1, 0, 0},
    .abi_major = EM_ABI_MAJOR,
    .abi_minor = EM_ABI_MINOR,
    .required_host_capabilities = EM_HOST_LOGGING | EM_HOST_SERVICES,
    .optional_host_capabilities = 0,
    .provided_capabilities = 0,
    .dependencies = dependencies,
    .dependency_count = 1,
    .flags = 0,
    .reserved = {0, 0},
};
EmHostContext host{};
eternal::sdk::Phase2Client client;
bool loaded{}, enabled{};
constexpr uint64_t operation(uint32_t value) { return UINT64_C(1) << (value - 1); }
constexpr uint64_t routeOperations = operation(EC_P2_OP_IDENTITY_READ) | operation(EC_P2_OP_ROLE_READ)
    | operation(EC_P2_OP_PERMISSION_CHECK) | operation(EC_P2_OP_ASSET_READ)
    | operation(EC_P2_OP_ASSET_ADD) | operation(EC_P2_OP_RECEIPT_READ) | operation(EC_P2_OP_OUTBOX_READ);
static_assert((routeOperations & (operation(EC_P2_OP_ROLE_GRANT) | operation(EC_P2_OP_ROLE_REVOKE)
    | operation(EC_P2_OP_ASSET_DEDUCT) | operation(EC_P2_OP_TRANSFER))) == 0);
bool same(EcId128 left, EcId128 right) { return left.low == right.low && left.high == right.high; }
bool nonzero(EcId128 value) { return value.low || value.high; }
EcStatus reply(EcUtf8Buffer* out, EcStatus status, const char* stage, std::string fields = {}) noexcept {
    if (!out) return EC_INVALID_ARGUMENT;
    try {
        const auto text = "{\"status\":" + std::to_string(status) + ",\"stage\":\"" + stage + "\"" + fields + "}";
        out->required = static_cast<uint32_t>(text.size() + 1);
        if (!out->data || out->capacity < out->required) return EC_BUFFER_TOO_SMALL;
        std::memcpy(out->data, text.c_str(), out->required);
        return status;
    } catch (...) { return EC_INTERNAL_ERROR; }
}
bool idempotency(EcUtf8View text, EcIdempotencyKey& key) {
    if (!text.data || text.length != 32 || text.reserved) return false;
    for (uint32_t i = 0; i < text.length; ++i)
        if ((text.data[i] < '0' || text.data[i] > '9') && (text.data[i] < 'a' || text.data[i] > 'f')) return false;
    const auto low = std::from_chars(text.data, text.data + 16, key.low, 16);
    const auto high = std::from_chars(text.data + 16, text.data + 32, key.high, 16);
    return low.ec == std::errc{} && high.ec == std::errc{} && nonzero(key);
}
EcStatus EC_CALL check(void*, const EcPhase2Invocation* invocation, EcUtf8Buffer* out) noexcept {
    try {
        if (!enabled) return reply(out, EC_NOT_READY, "lifecycle");
        if (!invocation || invocation->struct_size != sizeof(*invocation)
            || invocation->struct_version != EC_PHASE2_STRUCT_VERSION || invocation->reserved0
            || invocation->reserved1 || (invocation->flags & ~EC_P2_INVOCATION_DEVELOPMENT_ONLY)
            || !nonzero(invocation->invocation) || !nonzero(invocation->subject)
            || !nonzero(invocation->request_id)) return reply(out, EC_INVALID_ARGUMENT, "invocation");
        // Avoid an asset operation before the caller supplies room for the fixed diagnostic reply.
        if (!out) return EC_INVALID_ARGUMENT;
        if (!out->data || out->capacity < 1024) { out->required = 1024; return EC_BUFFER_TOO_SMALL; }
        EcIdempotencyKey key{};
        if (!idempotency(invocation->arguments, key)) return reply(out, EC_INVALID_ARGUMENT, "idempotency-key");
        auto diagnostic = client;
        diagnostic.setDevelopmentValidation(true); // Core-issued development grants still authorize each request.
        auto authorize = [&](uint32_t op, uint32_t asset, int64_t amount, EcPhase2RequestMeta& meta) {
            auto scope = eternal::sdk::output<EcPhase2InvocationAuthorization>();
            scope.caller_context = invocation->caller_context;
            scope.invocation = invocation->invocation;
            scope.target = invocation->subject;
            scope.operation = op; scope.asset = asset; scope.minor_units = amount;
            EcPhase2InvocationGrant grant{};
            const auto result = diagnostic.authorize(scope, grant);
            if (result != EC_OK) return result;
            if (!same(grant.subject, invocation->subject) || !nonzero(grant.capability)) return EcStatus{EC_DENIED};
            meta.caller_context = invocation->caller_context;
            meta.capability = grant.capability;
            meta.request_id = invocation->request_id;
            return EcStatus{EC_OK};
        };
        EcPhase2QueryRequest identityRequest{};
        identityRequest.target = invocation->subject;
        auto result = authorize(EC_P2_OP_IDENTITY_READ, EC_P2_ASSET_NONE, 0, identityRequest.meta);
        if (result != EC_OK) return reply(out, result, "authorize-identity");
        EcPhase2IdentitySnapshot identity{};
        std::array<char, EC_PHASE2_MAX_NAME_UTF8_BYTES + 1> name{};
        EcUtf8Buffer nameBuffer{name.data(), static_cast<uint32_t>(name.size()), 0};
        result = diagnostic.identity(identityRequest, identity, nameBuffer);
        if (result != EC_OK) return reply(out, result, "identity");
        EcPhase2QueryRequest rolesRequest{};
        rolesRequest.target = invocation->subject;
        result = authorize(EC_P2_OP_ROLE_READ, EC_P2_ASSET_NONE, 0, rolesRequest.meta);
        if (result != EC_OK) return reply(out, result, "authorize-roles");
        EcPhase2RoleSnapshot roles{};
        result = diagnostic.roles(rolesRequest, roles);
        if (result != EC_OK) return reply(out, result, "roles");
        auto asset = [&](uint32_t which, EcPhase2AssetSnapshot& value) {
            EcPhase2AssetRequest request{};
            request.target = invocation->subject; request.asset = which;
            const auto authorized = authorize(EC_P2_OP_ASSET_READ, which, 0, request.meta);
            return authorized == EC_OK ? diagnostic.asset(request, value) : authorized;
        };
        EcPhase2AssetSnapshot before{}, reputation{};
        result = asset(EC_P2_ASSET_MONEY, before);
        if (result != EC_OK) return reply(out, result, "money");
        result = asset(EC_P2_ASSET_REPUTATION, reputation);
        if (result != EC_OK) return reply(out, result, "reputation");
        EcPhase2PermissionRequest permissionRequest{};
        permissionRequest.subject = permissionRequest.target = invocation->subject;
        permissionRequest.operation = EC_P2_OP_ASSET_ADD; permissionRequest.asset = EC_P2_ASSET_MONEY;
        permissionRequest.minor_units = 100;
        result = authorize(EC_P2_OP_PERMISSION_CHECK, EC_P2_ASSET_MONEY, 100, permissionRequest.meta);
        if (result != EC_OK) return reply(out, result, "authorize-permission");
        EcPhase2PermissionDecision permission{};
        result = diagnostic.permission(permissionRequest, permission);
        if (result != EC_OK || !permission.allowed) return reply(out, result == EC_OK ? EC_DENIED : result, "permission");
        EcPhase2MutationRequest mutation{};
        mutation.target = invocation->subject; mutation.operation = EC_P2_OP_ASSET_ADD;
        mutation.asset = EC_P2_ASSET_MONEY; mutation.minor_units = 100;
        mutation.reason = {"sdk-route-test", 14, 0};
        result = authorize(EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100, mutation.meta);
        if (result != EC_OK) return reply(out, result, "authorize-add");
        mutation.meta.idempotency_key = key;
        EcPhase2Submission submission{};
        result = diagnostic.submit(mutation, submission);
        if (result != EC_OK) return reply(out, result, "add");
        EcPhase2ReceiptRequest receiptRequest{};
        receiptRequest.receipt_id = submission.receipt_id;
        result = authorize(EC_P2_OP_RECEIPT_READ, EC_P2_ASSET_NONE, 0, receiptRequest.meta);
        if (result != EC_OK) return reply(out, result, "authorize-receipt");
        EcPhase2Receipt receipt{};
        result = diagnostic.receipt(receiptRequest, receipt);
        if (result != EC_OK) return reply(out, result, "receipt");
        EcPhase2AssetSnapshot after{};
        result = asset(EC_P2_ASSET_MONEY, after);
        if (result != EC_OK) return reply(out, result, "money-after");
        EcPhase2OutboxRequest eventsRequest{};
        eventsRequest.limit = EC_PHASE2_MAX_OUTBOX_PAGE;
        result = authorize(EC_P2_OP_OUTBOX_READ, EC_P2_ASSET_NONE, 0, eventsRequest.meta);
        if (result != EC_OK) return reply(out, result, "authorize-outbox");
        std::array<EcPhase2OutboxEvent, EC_PHASE2_MAX_OUTBOX_PAGE> events{};
        EcPhase2OutboxBuffer eventsBuffer{};
        eventsBuffer.data = events.data(); eventsBuffer.capacity = static_cast<uint32_t>(events.size());
        result = diagnostic.outbox(eventsRequest, eventsBuffer);
        if (result != EC_OK) return reply(out, result, "outbox");
        uint32_t matching{};
        for (uint32_t i = 0; i < eventsBuffer.count; ++i)
            if (same(events[i].receipt_id, submission.receipt_id) && same(events[i].target, invocation->subject)) ++matching;
        return reply(out, EC_OK, "complete",
            ",\"identityFlags\":" + std::to_string(identity.flags)
            + ",\"roleMask\":" + std::to_string(roles.role_mask)
            + ",\"permissionAllowed\":true,\"moneyBeforeMinor\":" + std::to_string(before.minor_units)
            + ",\"moneyAfterMinor\":" + std::to_string(after.minor_units)
            + ",\"reputation\":" + std::to_string(reputation.minor_units)
            + ",\"addedMinor\":100,\"submissionReplayed\":" + (submission.flags & EC_P2_SUBMISSION_REPLAYED ? "true" : "false")
            + ",\"receiptState\":" + std::to_string(receipt.state)
            + ",\"receiptUnits\":" + std::to_string(receipt.minor_units)
            + ",\"receiptBalanceMinor\":" + std::to_string(receipt.target_balance)
            + ",\"receiptRevision\":" + std::to_string(receipt.target_revision)
            + ",\"outboxCount\":" + std::to_string(eventsBuffer.count)
            + ",\"matchingOutboxEvents\":" + std::to_string(matching));
    } catch (...) { return reply(out, EC_INTERNAL_ERROR, "exception"); }
}
}

extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor() noexcept {
    return &descriptor;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context) noexcept {
    if (loaded) return EM_CONFLICT;
    if (!context || context->struct_size != sizeof(*context)
        || context->struct_version != EM_STRUCT_VERSION || context->abi_major != EM_ABI_MAJOR
        || context->abi_minor < EM_ABI_MINOR) return EM_ABI_MISMATCH;
    if ((context->capabilities & descriptor.required_host_capabilities) != descriptor.required_host_capabilities
        || !context->instance || !context->log || !context->query_service) return EM_UNSUPPORTED;
    host = *context;
    loaded = true;
    return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept {
    try {
        if (!loaded) return EM_NOT_READY;
        if (enabled) return EM_CONFLICT;
        const auto result = eternal::sdk::Phase2Client::discover(host, EC_MODULE_CAP_ALL, client);
        if (result != EM_OK) return result;
        auto route = eternal::sdk::output<EcPhase2CommandRouteRequest>();
        route.route_id = {"check", 5, 0}; route.operation_mask = routeOperations; route.callback = check;
        const auto registered = client.registerRoute(route);
        if (registered != EC_OK) { client.reset(); return registered == EC_UNSUPPORTED ? EM_UNSUPPORTED : EM_INTERNAL_ERROR; }
        enabled = true;
        return EM_OK;
    } catch (...) { return EM_INTERNAL_ERROR; }
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept {
    // Host successfully revokes the binding and Core-owned routes before Disable.
    // Terminal shutdown may be on the shutdown thread: discard the client without calling game-thread services.
    enabled = false;
    client.reset();
    return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept {
    if (enabled) return EM_CONFLICT;
    loaded = false;
    client.reset();
    host = {};
    return EM_OK;
}

extern "C" EM_EXPORT EmStatus EM_CALL CoreValidationModule_Selfcheck(
    EcPhase2FeatureInfo* features, EcStatus* readStatus, uint32_t* routeRegistered) noexcept {
    if (!features || !readStatus || !routeRegistered) return EM_INVALID_ARGUMENT;
    *routeRegistered = enabled ? 1 : 0;
    if (!enabled) return EM_NOT_READY;
    auto production = client;
    production.setDevelopmentValidation(false);
    if (production.features(*features) != EC_OK) return EM_INTERNAL_ERROR;
    EcPhase2AssetSnapshot value{};
    *readStatus = production.asset({}, value);
    return EM_OK;
}
