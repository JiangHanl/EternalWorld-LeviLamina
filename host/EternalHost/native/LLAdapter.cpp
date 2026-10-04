#include "LLAdapter.hpp"
#include "EternalSDK/Core/native_ingress_abi.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "mc/entity/components/UserEntityIdentifierComponent.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandRawText.h"
#include "mc/world/level/Level.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <optional>
#include <unordered_map>

namespace eternal::adapter {
struct NativeParams { CommandRawText request; };
namespace {
EcUtf8View view(std::string_view text) { return {text.data(), static_cast<uint32_t>(text.size()), 0}; }
bool uuid(std::string_view text, EcUuid& out) {
    if (text.size() != 36) return false;
    unsigned byte = 0;
    for (unsigned i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (text[i++] != '-') return false; continue; }
        unsigned value{};
        auto result = std::from_chars(text.data()+i, text.data()+i+2, value, 16);
        if (result.ec != std::errc{} || result.ptr != text.data()+i+2) return false;
        out.bytes[byte++] = static_cast<uint8_t>(value); i += 2;
    }
    return byte == 16;
}
struct Identity {
    EcNativePlayerIdentity value{sizeof(EcNativePlayerIdentity), EC_NATIVE_INGRESS_STRUCT_VERSION};
    std::string name;
    std::string key;
    void bindName() { value.display_name = view(name); }
};
std::optional<Identity> authenticated(Player& player) {
    if (player.isSimulated()) return {};
    const auto* component = UserEntityIdentifierComponent::tryGetFromEntity(player.getEntityContext());
    if (!component || component->mAuthenticationType != PlayerAuthenticationType::Full) return {};
    const auto& info = component->mTrustedPlayerInfo.get();
    const auto& xuid = info.Xuid.get();
    if (xuid.empty() || xuid.front() == '0') return {};
    Identity result;
    auto parsed = std::from_chars(xuid.data(), xuid.data()+xuid.size(), result.value.trusted_xuid);
    if (parsed.ec != std::errc{} || parsed.ptr != xuid.data()+xuid.size() || !result.value.trusted_xuid) return {};
    const auto trusted = info.AuthenticatedUuid->asString();
    result.key = component->mClientUUID->asString();
    if (!uuid(trusted, result.value.trusted_uuid) || !uuid(result.key, result.value.client_uuid)
        || std::memcmp(&result.value.trusted_uuid, &result.value.client_uuid, sizeof(EcUuid))) return {};
    result.name = player.getRealName();
    result.value.is_fully_authenticated = 1;
    return result;
}
}

struct LLAdapter::Impl {
    eternal::host::Host& host;
    eternal::host::LogSink log;
    std::atomic_bool active{false};
    bool registered{};
    uint64_t sequence{};
    struct Session { EcUuid client; uint64_t generation; };
    // Associate the accepted generation with this actual engine connection.
    // A late disconnect from an earlier connection must not revoke a new one.
    std::unordered_map<Player*, Session> sessions;
    ll::event::ListenerPtr joins, disconnects, ticks;
    Impl(eternal::host::Host& h, eternal::host::LogSink sink) : host(h), log(std::move(sink)) {}
    const EcNativeIngressApi* ingress() {
        if (!active || !host.onBoundThread()) return nullptr;
        const EmServiceRequest request{sizeof(EmServiceRequest), EM_STRUCT_VERSION,
            {EC_NATIVE_INGRESS_SERVICE_ID, sizeof(EC_NATIVE_INGRESS_SERVICE_ID)-1, 0},
            EC_NATIVE_INGRESS_MAJOR, EC_NATIVE_INGRESS_MINOR, 0, 0};
        EmServiceReference reference{sizeof(EmServiceReference), EM_STRUCT_VERSION};
        if (host.queryService(request, reference) != EM_OK || reference.table_size < sizeof(EcNativeIngressApi)) return nullptr;
        const auto* api = static_cast<const EcNativeIngressApi*>(reference.table);
        if (!api || api->struct_size < sizeof(*api) || api->struct_version != EC_NATIVE_INGRESS_STRUCT_VERSION
            || api->api_major != EC_NATIVE_INGRESS_MAJOR || !api->authenticated_player || !api->native_command
            || !api->complete_action || !api->disconnect || !api->tick) return nullptr;
        return api;
    }
    EcRequestId requestId() {
        return {++sequence, static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count())};
    }
    void join(Player& player) {
        try {
            const auto* api = ingress(); auto identity = authenticated(player);
            if (!api || !identity) return;
            identity->bindName();
            EcNativeJoinResult result{sizeof(result), EC_NATIVE_INGRESS_STRUCT_VERSION};
            const auto status = api->authenticated_player(api->bridge_nonce, &identity->value, &result);
            if (status == EC_OK) { sessions[&player] = {identity->value.client_uuid, result.session_generation}; log(EM_LOG_INFO, "Core authenticated online player accepted"); }
            else log(EM_LOG_WARNING, "Core refused authenticated identity; no authority was granted");
        } catch (...) { log(EM_LOG_ERROR, "Core identity adapter failed safely"); }
    }
    void leave(Player& player) {
        try {
            const auto* api = ingress();
            if (!api) return;
            const auto found = sessions.find(&player);
            if (found == sessions.end()) return;
            const EcNativeDisconnectRequest request{sizeof(request), EC_NATIVE_INGRESS_STRUCT_VERSION,
                found->second.client, found->second.generation, 0};
            api->disconnect(api->bridge_nonce, &request); sessions.erase(found);
        } catch (...) { log(EM_LOG_ERROR, "Core disconnect adapter failed safely"); }
    }
    // Core produces all button meaning. This bridge never executes JSON commands.
    void form(Player& player, const EcNativeReply& reply, std::string_view payload) {
        auto identity = authenticated(player);
        if (!identity) return;
        const auto json = nlohmann::json::parse(payload);
        if (!json.is_object() || !json.contains("title") || !json["title"].is_string()
            || !json.contains("content") || !json["content"].is_string()
            || !json.contains("buttons") || !json["buttons"].is_array() || json["buttons"].size() > 16) return;
        ll::form::SimpleForm page(json["title"].get<std::string>(), json["content"].get<std::string>());
        for (const auto& button : json["buttons"]) {
            if (button.is_string()) page.appendButton(button.get<std::string>());
            else if (button.is_object() && button.contains("text") && button["text"].is_string()) page.appendButton(button["text"].get<std::string>());
            else return;
        }
        const auto target = identity->value.trusted_uuid;
        const auto targetXuid = identity->value.trusted_xuid;
        const auto action = reply.pending_action;
        const auto count = static_cast<int>(json["buttons"].size());
        page.sendTo(player, [this, target, targetXuid, action, count](Player& sender, int selected, ll::form::FormCancelReason cancelled) {
            try {
                auto dispatch = host.guardTrustedDispatch();
                if (!dispatch) return;
                const auto* api = ingress(); auto current = authenticated(sender);
                if (!api || !current || current->value.trusted_xuid != targetXuid
                    || std::memcmp(&current->value.trusted_uuid, &target, sizeof(target))) return;
                current->bindName();
                EcNativeActionCompletionRequest request{sizeof(request), EC_NATIVE_INGRESS_STRUCT_VERSION};
                request.request_id = requestId(); request.player = current->value; request.pending_action = action;
                request.selected_button = cancelled || selected < 0 || selected >= count ? -1 : selected;
                std::array<char, EC_NATIVE_MAX_REPLY_UTF8_BYTES> storage{};
                EcUtf8Buffer output{storage.data(), static_cast<uint32_t>(storage.size()), 0};
                EcNativeReply response{sizeof(response), EC_NATIVE_INGRESS_STRUCT_VERSION};
                const auto status = api->complete_action(api->bridge_nonce, &request, &response, &output);
                if (status == EC_BUFFER_TOO_SMALL || !output.required || output.required > storage.size()) return;
                sender.sendMessage(std::string_view(storage.data(), output.required-1));
            } catch (...) { log(EM_LOG_ERROR, "Core form completion failed safely"); }
        });
    }
    void command(CommandOrigin const& origin, CommandOutput& output, const NativeParams& params) {
        try {
            auto dispatch = host.guardTrustedDispatch();
            if (!dispatch) { output.error("EternalCore is not ready"); return; }
            const auto* api = ingress();
            if (!api) { output.error("EternalCore is not ready"); return; }
            EcNativeCommandRequest request{sizeof(request), EC_NATIVE_INGRESS_STRUCT_VERSION};
            request.request_id = requestId();
            request.command_text = view(params.request.mText);
            std::optional<Identity> identity;
            Player* player = nullptr;
            if (origin.getOriginType() == CommandOriginType::Player) {
                auto* entity = origin.getEntity();
                if (!entity || !entity->isPlayer()) { output.error("Authenticated player origin required"); return; }
                player = static_cast<Player*>(entity); identity = authenticated(*player);
                if (!identity) { output.error("BDS full authentication required"); return; }
                identity->bindName(); request.player = identity->value; request.origin = EC_NATIVE_ORIGIN_PLAYER;
            } else if (origin.getOriginType() == CommandOriginType::DedicatedServer) request.origin = EC_NATIVE_ORIGIN_SERVER_CONSOLE;
            else { output.error("Unsupported command origin"); return; }
            std::array<char, EC_NATIVE_MAX_REPLY_UTF8_BYTES> storage{};
            EcUtf8Buffer text{storage.data(), static_cast<uint32_t>(storage.size()), 0};
            EcNativeReply reply{sizeof(reply), EC_NATIVE_INGRESS_STRUCT_VERSION};
            const auto status = api->native_command(api->bridge_nonce, &request, &reply, &text);
            if (status == EC_BUFFER_TOO_SMALL || !text.required || text.required > storage.size()) { output.error("Core diagnostic reply unavailable"); return; }
            const std::string payload(storage.data(), text.required-1);
            if (status != EC_OK || reply.result != EC_OK) { output.error(payload); return; }
            if (reply.flags & EC_NATIVE_REPLY_SIMPLE_FORM) {
                if (!player) { output.error("Player UI requires authenticated player origin"); return; }
                form(*player, reply, payload); output.success("Core authority validation form opened");
            } else output.success(payload);
        } catch (...) { output.error("Core native adapter failed safely"); }
    }
    bool enable() {
        active = true;
        if (!registered) {
            auto& bus = ll::event::EventBus::getInstance();
            joins = bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& e) { if (active) join(e.self()); });
            disconnects = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& e) { if (active) leave(e.self()); });
            ticks = bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) {
                if (const auto* api = ingress()) {
                    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    api->tick(api->bridge_nonce, static_cast<uint64_t>(millis));
                }
            });
            if (!joins || !disconnects || !ticks) {
                active = false;
                if (joins) bus.removeListener<ll::event::PlayerJoinEvent>(joins);
                if (disconnects) bus.removeListener<ll::event::PlayerDisconnectEvent>(disconnects);
                if (ticks) bus.removeListener<ll::event::ServerLevelTickEvent>(ticks);
                joins.reset(); disconnects.reset(); ticks.reset();
                return false;
            }
            ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
                "ecore", "永恒核心：身份与权限诊断", CommandPermissionLevel::Any)
                .overload<NativeParams>().text("native").required("request")
                .execute([this](CommandOrigin const& origin, CommandOutput& output, NativeParams const& params) { command(origin, output, params); });
            registered = true;
        }
        sessions.clear();
        if (auto level = ll::service::getLevel()) level->forEachPlayer([this](Player& player) { join(player); return true; });
        return true;
    }
};
LLAdapter::LLAdapter(eternal::host::Host& h, eternal::host::LogSink log) : impl_(std::make_unique<Impl>(h,std::move(log))) {}
LLAdapter::~LLAdapter() = default;
bool LLAdapter::enable() { return impl_->enable(); }
void LLAdapter::disable() noexcept { impl_->active = false; }
}
