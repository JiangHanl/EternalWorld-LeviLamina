#include "EternalSDK/Core/Phase2Client.hpp"
#include "domain/Commerce.hpp"
#include <array>
#include <charconv>
#include <cstring>
#include <chrono>
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
EcConsumerHandle deliveryConsumer{};
uint64_t deliverySerial{};
bool loaded{}, enabled{};

bool zero(EcId128 value) { return value.low == 0 && value.high == 0; }
std::string hex128(EcId128 value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[i] = digits[(value.high >> ((15 - i) * 4)) & 15];
        out[16 + i] = digits[(value.low >> ((15 - i) * 4)) & 15];
    }
    return out;
}
int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
EcId128 parse128(std::string_view value) {
    EcId128 out{};
    if (value.size() != 32)
        return out;
    for (int i = 0; i < 16; ++i) {
        const int a = hexValue(value[i]);
        const int b = hexValue(value[16 + i]);
        if (a < 0 || b < 0)
            return {};
        out.high = (out.high << 4) | static_cast<uint64_t>(a);
        out.low = (out.low << 4) | static_cast<uint64_t>(b);
    }
    return out;
}
std::int64_t wall() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
EcStatus replyText(EcUtf8Buffer *reply, std::string_view message, EcStatus status) {
    const auto length = static_cast<uint32_t>(message.size() + 1);
    if (!reply->data || reply->capacity < length)
        return EC_BUFFER_TOO_SMALL;
    std::memcpy(reply->data, message.data(), message.size());
    reply->data[message.size()] = 0;
    reply->required = length;
    return status;
}

EcStatus pollDelivery() noexcept {
    try {
        if (!enabled || !commerce)
            return EC_NOT_READY;
        std::array<EcPhase2ConsumerEvent, 100> events{};
        EcPhase2ConsumerQueryRequest query{};
        query.consumer = deliveryConsumer;
        query.limit = 100;
        EcPhase2ConsumerBuffer buffer{};
        buffer.data = events.data();
        buffer.capacity = 100;
        auto result = client.queryConsumer(query, buffer);
        if (result != EC_OK)
            return result;
        for (uint32_t i = 0; i < buffer.count; ++i) {
            const auto &event = events[i];
            const auto source = "outbox:" + std::to_string(event.event.event_id);
            if (commerce->deliveryBySource(source).status == eternal::commerce::Status::NotFound) {
                const auto created = commerce->createDelivery(hex128(event.event.target), "outbox",
                                                              "{}", source, wall());
                if (created.status != eternal::commerce::Status::Ok)
                    return EC_INTERNAL_ERROR;
            }
            EcPhase2ConsumerEventRequest ack{};
            ack.consumer = deliveryConsumer;
            ack.delivery = event.delivery;
            ack.event_id = event.event.event_id;
            ack.request_id = {++deliverySerial, UINT64_C(0xcc01)};
            result = client.acknowledgeConsumer(ack);
            if (result != EC_OK)
                return result;
        }
        return EC_OK;
    } catch (...) {
        return EC_INTERNAL_ERROR;
    }
}

void EM_CALL onOutboxNotice(void *, const EmEvent *) noexcept { (void)pollDelivery(); }

EcStatus EM_CALL onTransfer(void *, const EcPhase2Invocation *invocation,
                            EcUtf8Buffer *reply) noexcept {
    try {
    if (!invocation || !reply || !reply->data || !reply->capacity)
        return EC_INVALID_ARGUMENT;
        if (!commerce)
            return replyText(reply, "Commerce not ready", EC_NOT_READY);
        const std::string_view args(invocation->arguments.data, invocation->arguments.length);
        const auto space = args.find(' ');
        if (space != 32)
            return replyText(reply, "usage: transfer <recipientId32> <amountMinor>",
                             EC_INVALID_ARGUMENT);
        const auto recipient = parse128(args.substr(0, 32));
        std::int64_t amount = 0;
        const auto amountText = args.substr(space + 1);
        const auto parsed =
            std::from_chars(amountText.data(), amountText.data() + amountText.size(), amount);
        if (zero(recipient) || parsed.ec != std::errc{} ||
            parsed.ptr != amountText.data() + amountText.size() || amount <= 0)
            return replyText(reply, "invalid recipient or amount", EC_INVALID_ARGUMENT);
        const auto senderUuid = hex128(invocation->subject);
        const auto transfer = commerce->recordTransfer(senderUuid, amount,
                                                       hex128(invocation->request_id), wall());
        if (transfer.status != eternal::commerce::Status::Ok)
            return replyText(reply, "Commerce transfer rejected", EC_DENIED);
        EcPhase2InvocationAuthorization auth{};
        auth.invocation = invocation->invocation;
        auth.target = invocation->subject;
        auth.recipient = recipient;
        auth.operation = EC_P2_OP_TRANSFER;
        auth.asset = EC_P2_ASSET_MONEY;
        auth.minor_units = transfer.netMinor;
        EcPhase2InvocationGrant grant{};
        auto status = client.authorize(auth, grant);
        if (status != EC_OK)
            return replyText(reply, "Commerce authorize denied", status);
        EcPhase2MutationRequest mutation{};
        mutation.meta.capability = grant.capability;
        mutation.meta.request_id = invocation->request_id;
        mutation.meta.idempotency_key = invocation->request_id;
        mutation.target = invocation->subject;
        mutation.recipient = recipient;
        mutation.operation = EC_P2_OP_TRANSFER;
        mutation.asset = EC_P2_ASSET_MONEY;
        mutation.minor_units = transfer.netMinor;
        mutation.reason = ecView("Commerce transfer");
        EcPhase2Submission submission{};
        status = client.submit(mutation, submission);
        if (status != EC_OK)
            return replyText(reply, "Commerce submit denied", status);
        return replyText(reply, "Commerce transfer accepted", EC_OK);
    } catch (...) {
        return EC_INTERNAL_ERROR;
    }
}

EcStatus EM_CALL onGift(void *, const EcPhase2Invocation *invocation,
                        EcUtf8Buffer *reply) noexcept {
    try {
        if (!invocation || !reply || !reply->data || !reply->capacity)
            return EC_INVALID_ARGUMENT;
        if (!commerce)
            return replyText(reply, "Commerce not ready", EC_NOT_READY);
        const std::string_view args(invocation->arguments.data, invocation->arguments.length);
        const auto space = args.find(' ');
        if (space == std::string_view::npos)
            return replyText(reply, "usage: gift <shareMinor> <recipients>", EC_INVALID_ARGUMENT);
        std::int64_t shareMinor = 0, recipients = 0;
        const auto first = std::from_chars(args.data(), args.data() + space, shareMinor);
        const auto rest = args.substr(space + 1);
        const auto second =
            std::from_chars(rest.data(), rest.data() + rest.size(), recipients);
        if (first.ec != std::errc{} || first.ptr != args.data() + space || second.ec != std::errc{} ||
            second.ptr != rest.data() + rest.size())
            return replyText(reply, "invalid gift parameters", EC_INVALID_ARGUMENT);
        const auto senderUuid = hex128(invocation->subject);
        const auto gift = commerce->createGift(senderUuid, shareMinor, recipients, wall());
        if (gift.status != eternal::commerce::Status::Ok)
            return replyText(reply, "Gift creation rejected", EC_DENIED);
        const auto deduct = gift.gift->totalMinor + gift.gift->feeMinor;
        EcPhase2InvocationAuthorization auth{};
        auth.invocation = invocation->invocation;
        auth.target = invocation->subject;
        auth.operation = EC_P2_OP_ASSET_DEDUCT;
        auth.asset = EC_P2_ASSET_MONEY;
        auth.minor_units = deduct;
        EcPhase2InvocationGrant grant{};
        auto status = client.authorize(auth, grant);
        if (status != EC_OK)
            return replyText(reply, "Gift authorize denied", status);
        EcPhase2MutationRequest mutation{};
        mutation.meta.capability = grant.capability;
        mutation.meta.request_id = invocation->request_id;
        mutation.meta.idempotency_key = invocation->request_id;
        mutation.target = invocation->subject;
        mutation.operation = EC_P2_OP_ASSET_DEDUCT;
        mutation.asset = EC_P2_ASSET_MONEY;
        mutation.minor_units = deduct;
        mutation.reason = ecView("Commerce gift");
        EcPhase2Submission submission{};
        status = client.submit(mutation, submission);
        if (status != EC_OK)
            return replyText(reply, "Gift submit denied", status);
        return replyText(reply, "Commerce gift accepted", EC_OK);
    } catch (...) {
        return EC_INTERNAL_ERROR;
    }
}

EcStatus EM_CALL onList(void *, const EcPhase2Invocation *invocation,
                        EcUtf8Buffer *reply) noexcept {
    try {
        if (!invocation || !reply || !reply->data || !reply->capacity)
            return EC_INVALID_ARGUMENT;
        if (!commerce)
            return replyText(reply, "Commerce not ready", EC_NOT_READY);
        const std::string_view args(invocation->arguments.data, invocation->arguments.length);
        const auto space = args.find(' ');
        if (space == std::string_view::npos)
            return replyText(reply, "usage: list <item> <priceMinor>", EC_INVALID_ARGUMENT);
        const auto item = args.substr(0, space);
        std::int64_t price = 0;
        const auto rest = args.substr(space + 1);
        const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), price);
        if (item.empty() || parsed.ec != std::errc{} || parsed.ptr != rest.data() + rest.size() ||
            price <= 0)
            return replyText(reply, "invalid list parameters", EC_INVALID_ARGUMENT);
        const auto result =
            commerce->listConsignment(hex128(invocation->subject), item, "{tag:1}", item, price,
                                     wall());
        if (result.status != eternal::commerce::Status::Ok)
            return replyText(reply, "List creation rejected", EC_DENIED);
        return replyText(reply, std::to_string(result.consignment->id), EC_OK);
    } catch (...) {
        return EC_INTERNAL_ERROR;
    }
}

EcStatus EM_CALL onBuy(void *, const EcPhase2Invocation *invocation,
                       EcUtf8Buffer *reply) noexcept {
    try {
        if (!invocation || !reply || !reply->data || !reply->capacity)
            return EC_INVALID_ARGUMENT;
        if (!commerce)
            return replyText(reply, "Commerce not ready", EC_NOT_READY);
        const std::string_view args(invocation->arguments.data, invocation->arguments.length);
        std::int64_t id = 0;
        const auto parsed = std::from_chars(args.data(), args.data() + args.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != args.data() + args.size() || id <= 0)
            return replyText(reply, "usage: buy <consignmentId>", EC_INVALID_ARGUMENT);
        const auto buyerUuid = hex128(invocation->subject);
        if (commerce->buyConsignment(id, buyerUuid, wall()) != eternal::commerce::Status::Ok)
            return replyText(reply, "Buy rejected", EC_DENIED);
        const auto consignment = commerce->consignment(id).consignment;
        if (!consignment)
            return replyText(reply, "Consignment missing", EC_NOT_FOUND);
        const auto seller = parse128(consignment->sellerUuid);
        EcPhase2InvocationAuthorization auth{};
        auth.invocation = invocation->invocation;
        auth.target = invocation->subject;
        auth.recipient = seller;
        auth.operation = EC_P2_OP_TRANSFER;
        auth.asset = EC_P2_ASSET_MONEY;
        auth.minor_units = consignment->priceMinor;
        EcPhase2InvocationGrant grant{};
        auto status = client.authorize(auth, grant);
        if (status != EC_OK)
            return replyText(reply, "Buy authorize denied", status);
        EcPhase2MutationRequest mutation{};
        mutation.meta.capability = grant.capability;
        mutation.meta.request_id = invocation->request_id;
        mutation.meta.idempotency_key = invocation->request_id;
        mutation.target = invocation->subject;
        mutation.recipient = seller;
        mutation.operation = EC_P2_OP_TRANSFER;
        mutation.asset = EC_P2_ASSET_MONEY;
        mutation.minor_units = consignment->priceMinor;
        mutation.reason = ecView("Commerce buy");
        EcPhase2Submission submission{};
        status = client.submit(mutation, submission);
        if (status != EC_OK)
            return replyText(reply, "Buy submit denied", status);
        const auto delivery = commerce->createDelivery(buyerUuid, consignment->item,
                                                       consignment->nbt, "consignment", wall());
        if (delivery.status != eternal::commerce::Status::Ok)
            return replyText(reply, "Delivery creation failed", EC_INTERNAL_ERROR);
        return replyText(reply, "Commerce buy accepted", EC_OK);
    } catch (...) {
        return EC_INTERNAL_ERROR;
    }
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
#ifdef ETERNAL_COMMERCE_VALIDATION_BUILD
        client.setDevelopmentValidation(true);
#endif
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
        EcPhase2CommandRouteRequest giftRoute{};
        giftRoute.route_id = ecView("gift");
        giftRoute.operation_mask = UINT64_C(1) << (EC_P2_OP_ASSET_DEDUCT - 1);
        giftRoute.callback = onGift;
        giftRoute.user = nullptr;
        const auto giftRegistered = client.registerRoute(giftRoute);
        if (giftRegistered != EC_OK) {
            commerce.reset();
            client.reset();
            return giftRegistered;
        }
        EcPhase2CommandRouteRequest listRoute{};
        listRoute.route_id = ecView("list");
        listRoute.operation_mask = UINT64_C(1) << (EC_P2_OP_ASSET_READ - 1);
        listRoute.callback = onList;
        listRoute.user = nullptr;
        const auto listRegistered = client.registerRoute(listRoute);
        if (listRegistered != EC_OK) {
            commerce.reset();
            client.reset();
            return listRegistered;
        }
        EcPhase2CommandRouteRequest buyRoute{};
        buyRoute.route_id = ecView("buy");
        buyRoute.operation_mask = UINT64_C(1) << (EC_P2_OP_TRANSFER - 1);
        buyRoute.callback = onBuy;
        buyRoute.user = nullptr;
        const auto buyRegistered = client.registerRoute(buyRoute);
        if (buyRegistered != EC_OK) {
            commerce.reset();
            client.reset();
            return buyRegistered;
        }
#ifdef ETERNAL_COMMERCE_VALIDATION_BUILD
        EcPhase2ConsumerRegistrationRequest registration{};
        registration.local_key = ecView("delivery");
        EcPhase2ConsumerRegistration consumerRegistration{};
        if (client.registerConsumer(registration, consumerRegistration) != EC_OK) {
            commerce.reset();
            client.reset();
            return EM_UNSUPPORTED;
        }
        deliveryConsumer = consumerRegistration.consumer;
        EmSubscription subscription{sizeof(subscription), EM_STRUCT_VERSION,
                                    view("core.outbox.changed"), onOutboxNotice, nullptr, 0};
        uint64_t token{};
        if (host.subscribe(host.instance, &subscription, &token) != EM_OK) {
            deliveryConsumer = {};
            commerce.reset();
            client.reset();
            return EM_UNSUPPORTED;
        }
        (void)pollDelivery();
#endif
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
        EcPhase2RouteRemovalRequest giftRemoval{};
        giftRemoval.route_id = ecView("gift");
        client.unregisterRoute(giftRemoval);
        EcPhase2RouteRemovalRequest listRemoval{};
        listRemoval.route_id = ecView("list");
        client.unregisterRoute(listRemoval);
        EcPhase2RouteRemovalRequest buyRemoval{};
        buyRemoval.route_id = ecView("buy");
        client.unregisterRoute(buyRemoval);
    }
    deliveryConsumer = {};
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
