#include "../../modules/EternalCore/domain/Core.hpp"
#include "../../modules/EternalCore/runtime/Runtime.hpp"
#include <EternalSDK/Core/Phase2Client.hpp>
#include <Windows.h>
#include <array>
#include <barrier>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef ETERNAL_CORE_RUNTIME_TESTING
#error SyntheticMultiUserTests requires private test hooks, never a production target
#endif
#ifndef ETERNAL_CORE_VALIDATION_BUILD
#error SyntheticMultiUserTests requires its isolated validation compilation macro
#endif

using eternal::core::runtime::Runtime;
using eternal::sdk::Phase2Client;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr char ownerXuid[] = "700000";
constexpr char moduleId[] = "synthetic-harness";
constexpr char reason[] = "synthetic integration";
enum User : std::size_t {
    Owner,
    Administrator,
    Builder,
    Moderator,
    PlayerA,
    PlayerB,
    OpOnly,
    Revoked,
    UserCount
};
constexpr std::array<const char *, UserCount> names{
    "SyntheticOwner",         "SyntheticAdministrator", "SyntheticBuilder",      "SyntheticModerator",
    "SyntheticNormalPlayerA", "SyntheticNormalPlayerB", "SyntheticOpOnlyPlayer", "SyntheticRevokedPlayer"};

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T dto() {
    T value{};
    value.struct_size = sizeof(T);
    value.struct_version = 1;
    return value;
}
bool same(EcId128 a, EcId128 b) { return a.low == b.low && a.high == b.high; }
bool nonzero(EcId128 a) { return a.low || a.high; }
std::string hex(EcId128 id) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(32, '0');
    for (unsigned i = 0; i < 16; ++i) {
        out[15 - i] = digits[(id.high >> (i * 4)) & 15];
        out[31 - i] = digits[(id.low >> (i * 4)) & 15];
    }
    return out;
}
std::string uuid(EcUuid id) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        out += digits[id.bytes[i] >> 4];
        out += digits[id.bytes[i] & 15];
    }
    return out;
}

// This private test reader can open only the fixture's temporary synthetic DB.
// No DB handle or private domain object crosses the public module ABI.
class ReadDb final {
    sqlite3 *db_{};

  public:
    explicit ReadDb(const fs::path &path) {
        if (sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (db_)
                sqlite3_close(db_);
            throw std::runtime_error("Synthetic database read failed");
        }
    }
    ~ReadDb() { sqlite3_close(db_); }
    ReadDb(const ReadDb &) = delete;
    ReadDb &operator=(const ReadDb &) = delete;
    Json rows(const char *sql) const {
        sqlite3_stmt *raw{};
        require(sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) == SQLITE_OK, "Synthetic query prepare");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
        require(sqlite3_stmt_readonly(raw) != 0, "Test assertion attempted a DB write");
        Json result = Json::array();
        int code{};
        while ((code = sqlite3_step(raw)) == SQLITE_ROW) {
            Json row = Json::array();
            for (int i = 0; i < sqlite3_column_count(raw); ++i) {
                if (sqlite3_column_type(raw, i) == SQLITE_INTEGER)
                    row.push_back(sqlite3_column_int64(raw, i));
                else if (sqlite3_column_type(raw, i) == SQLITE_NULL)
                    row.push_back(nullptr);
                else
                    row.push_back(reinterpret_cast<const char *>(sqlite3_column_text(raw, i)));
            }
            result.push_back(std::move(row));
        }
        require(code == SQLITE_DONE, "Synthetic query execution");
        return result;
    }
    std::int64_t count(const char *sql) const { return rows(sql).at(0).at(0).get<std::int64_t>(); }
    void consistent() const {
        require(rows("PRAGMA integrity_check") == Json::array({Json::array({"ok"})}),
                "Integrity check failed");
        require(rows("PRAGMA foreign_key_check").empty(), "Foreign key check failed");
        require(count("SELECT count(*) FROM accounts a WHERE balance!=COALESCE((SELECT sum(delta) FROM "
                      "entries e WHERE e.uuid=a.uuid AND e.asset=a.asset),0)") == 0,
                "Ledger does not reconcile accounts");
        require(count("SELECT count(*) FROM transactions t LEFT JOIN receipts r ON "
                      "r.transactionId=t.transactionId WHERE r.transactionId IS NULL") == 0,
                "Transaction missing durable receipt");
        require(count("SELECT count(*) FROM entries e JOIN transactions t ON t.transactionId=e.transactionId "
                      "WHERE t.status='rejected'") == 0,
                "Rejected transaction wrote ledger");
        require(count("SELECT count(*) FROM outbox e JOIN transactions t ON t.transactionId=e.transactionId "
                      "WHERE t.status='rejected'") == 0,
                "Rejected transaction wrote successful outbox");
    }
};

struct Subject {
    EcNativePlayerIdentity identity{};
    EcNativeJoinResult joined{};
    // Synthetic engine state only. It is deliberately NOT an authorization input.
    bool nativeOp{};
};
struct Outcome {
    EcStatus status{};
    EcPhase2Submission submission{};
};

struct Harness {
    static inline unsigned serial{};
    fs::path root =
        fs::temp_directory_path() / ("EternalSyntheticMultiUser_" + std::to_string(GetCurrentProcessId()) +
                                     "_" + std::to_string(++serial));
    std::array<Subject, UserCount> users{};
    std::unique_ptr<Runtime> runtime;
    EmHostContext host = dto<EmHostContext>();
    EcNativeModuleBindingResult binding{};
    Phase2Client client;
    std::uint64_t clock{1000}, requestSerial{1}, keySerial{1};
    unsigned noticeCount{};
    EcPhase2MutationRequest revokedRequest{};
    const std::thread::id serviceThread = std::this_thread::get_id();

    Harness() {
        fs::create_directories(root / "config");
        const auto caps = Json::array({EC_MODULE_CAP_PLAYER_READ_NAME, EC_MODULE_CAP_PERMISSION_CHECK_NAME,
                                       EC_MODULE_CAP_MONEY_NAME, EC_MODULE_CAP_REPUTATION_NAME,
                                       EC_MODULE_CAP_AUDIT_NAME, EC_MODULE_CAP_EVENTS_NAME});
        std::ofstream(root / "config" / "core.json") << Json{
            {"ownerXuid", ownerXuid},
            {"developmentValidation", false},
            {"validatedAssets", false},
            {"moduleCapabilities",
             {{moduleId, caps}, {"core", caps}}}}.dump();
        for (std::size_t i = 0; i < users.size(); ++i) {
            auto &p = users[i];
            p.identity = dto<EcNativePlayerIdentity>();
            p.identity.trusted_uuid.bytes[0] = 0xe7;
            p.identity.trusted_uuid.bytes[6] = 0x40;
            p.identity.trusted_uuid.bytes[8] = 0x80;
            p.identity.trusted_uuid.bytes[15] = static_cast<std::uint8_t>(i + 1);
            p.identity.client_uuid = p.identity.trusted_uuid;
            p.identity.trusted_xuid = 700000 + i;
            p.identity.display_name = {names[i], static_cast<std::uint32_t>(std::strlen(names[i])), 0};
            p.identity.is_fully_authenticated = 1;
            p.nativeOp = i == OpOnly;
        }
        host.abi_major = EM_ABI_MAJOR;
        host.abi_minor = EM_ABI_MINOR;
        host.capabilities = EM_HOST_SERVICES | EM_HOST_EVENTS;
        host.instance = this;
        host.publish_service = [](void *, const EmServiceOffer *) noexcept -> EmStatus { return EM_OK; };
        host.publish_event = [](void *instance, const EmEvent *event) noexcept -> EmStatus {
            if (!event || event->data_size != sizeof(EcPhase2OutboxNotice))
                return EM_ABI_MISMATCH;
            ++static_cast<Harness *>(instance)->noticeCount;
            return EM_OK;
        };
        host.query_service = [](void *instance, const EmServiceRequest *request,
                                EmServiceReference *out) noexcept -> EmStatus {
            if (!request || !out || !request->required_capabilities)
                return EM_INVALID_ARGUMENT;
            auto &f = *static_cast<Harness *>(instance);
            if ((request->required_capabilities & ~EC_MODULE_CAP_ALL) || !f.binding.scoped_api)
                return EM_UNSUPPORTED;
            *out = dto<EmServiceReference>();
            out->table = f.binding.scoped_api;
            out->table_size = f.binding.table_size;
            out->api_major = EC_PHASE2_API_MAJOR;
            out->api_minor = EC_PHASE2_API_MINOR;
            out->capabilities = EC_MODULE_CAP_ALL;
            out->generation = 1;
            return EM_OK;
        };
        start();
        setRole(Administrator, EC_P2_ROLE_ADMINISTRATOR, true);
        setRole(Builder, EC_P2_ROLE_BUILDER, true);
        setRole(Moderator, EC_P2_ROLE_MODERATOR, true);
        setRole(Revoked, EC_P2_ROLE_ECONOMY_MANAGER, true);
        revokedRequest = scoped(Revoked, Revoked, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100);
        setRole(Revoked, EC_P2_ROLE_ECONOMY_MANAGER, false);
    }
    ~Harness() {
        client.reset();
        runtime.reset();
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    void threadCheck() const {
        require(std::this_thread::get_id() == serviceThread,
                "Runtime must be serialized on its service thread");
    }
    const EcNativeIngressApi *ingress() const { return runtime->nativeApi(); }
    fs::path dbPath() const { return root / "data" / "core.sqlite3"; }
    EcRequestId requestId() { return {requestSerial++, 0x53594e5448455449}; }
    EcId128 key() { return {keySerial++, 0x53594e4b45590001}; }
    void authenticate(User user) {
        threadCheck();
        auto &p = users[user];
        auto result = dto<EcNativeJoinResult>();
        require(ingress()->authenticated_player(ingress()->bridge_nonce, &p.identity, &result) == EC_OK,
                "Synthetic trusted authenticate");
        p.joined = result;
    }
    void start() {
        threadCheck();
        runtime = std::make_unique<Runtime>(root / "config", root / "data", [this] { return clock; });
        require(runtime->load() && runtime->enable(host) == EM_OK, "Synthetic Core start");
        auto feature = dto<EcPhase2FeatureInfo>();
        require(runtime->phase2Api()->get_phase2_features(&feature) == EC_OK && feature.enabled == 0,
                "Production features must start closed");
        for (std::size_t i = 0; i < UserCount; ++i)
            authenticate(static_cast<User>(i));
        auto request = dto<EcNativeModuleBindingRequest>();
        request.module_id = {moduleId, sizeof(moduleId) - 1, 0};
        request.module_generation = 1;
        request.approved_module_capabilities = EC_MODULE_CAP_ALL;
        request.approved_permissions = EC_P2_PERMISSION_ALL;
        request.money_limit_per_request = 1000000;
        request.reputation_limit_per_request = 10000;
        request.money_budget_per_period = 6000000;
        request.reputation_budget_per_period = 60000;
        request.budget_period_ms = 60000;
        binding = dto<EcNativeModuleBindingResult>();
        require(ingress()->bind_module(ingress()->bridge_nonce, &request, &binding) == EC_OK,
                "Synthetic module scope");
        require(Phase2Client::discover(host, EC_MODULE_CAP_ALL, client) == EM_OK, "Formal SDK discovery");
        // This macro exists only in the test executable. Production feature bits remain zero.
        require(runtime->testEnableFeatures(EC_P2_FEATURE_ALL) == EC_OK,
                "Private synthetic feature injection");
    }
    void stop() {
        threadCheck();
        require(runtime->disable() == EM_OK, "Synthetic Core stop");
        client.reset();
        runtime.reset();
        binding = {};
    }
    EcStatus disconnect(User user, std::uint64_t generation = 0) {
        threadCheck();
        auto request = dto<EcNativeDisconnectRequest>();
        request.client_uuid = users[user].identity.client_uuid;
        request.expected_session_generation = generation ? generation : users[user].joined.session_generation;
        return ingress()->disconnect(ingress()->bridge_nonce, &request);
    }
    EcPhase2MutationRequest scoped(User actor, User target, std::uint32_t operation, std::uint32_t asset = 0,
                                   std::int64_t amount = 0, EcId128 idempotency = {}, std::uint64_t role = 0,
                                   EcPlayerId recipient = {}) {
        threadCheck();
        EcPhase2MutationRequest request{};
        request.meta.struct_size = sizeof(request);
        request.meta.struct_version = 1;
        request.meta.caller_context = binding.caller_context;
        request.meta.request_id = requestId();
        request.meta.idempotency_key = nonzero(idempotency) ? idempotency : key();
        request.target = users[target].joined.player_id;
        request.recipient = recipient;
        request.operation = operation;
        request.asset = asset;
        request.minor_units = amount;
        request.role_mask = role;
        request.reason = {reason, sizeof(reason) - 1, 0};
        require(runtime->testIssueCapability(users[actor].identity, request, request.meta.capability) ==
                    EC_OK,
                "Private synthetic capability injection");
        return request;
    }
    Outcome submit(const EcPhase2MutationRequest &request) {
        threadCheck();
        Outcome out{};
        out.status = client.submit(request, out.submission);
        return out;
    }
    Outcome asset(User actor, User target, std::uint32_t which, std::int64_t amount,
                  EcId128 idempotency = {}) {
        return submit(scoped(actor, target, amount < 0 ? EC_P2_OP_ASSET_DEDUCT : EC_P2_OP_ASSET_ADD, which,
                             amount < 0 ? -amount : amount, idempotency));
    }
    Outcome transfer(User actor, User recipient, std::int64_t amount, EcId128 idempotency = {}) {
        return submit(scoped(actor, actor, EC_P2_OP_TRANSFER, EC_P2_ASSET_MONEY, amount, idempotency, 0,
                             users[recipient].joined.player_id));
    }
    void setRole(User target, std::uint64_t role, bool enabled) {
        const auto result = submit(
            scoped(Owner, target, enabled ? EC_P2_OP_ROLE_GRANT : EC_P2_OP_ROLE_REVOKE, 0, 0, {}, role));
        require(result.status == EC_OK && nonzero(result.submission.receipt_id), "Owner role change");
    }
    EcPhase2IdentitySnapshot identity(User actor) {
        const auto scope = scoped(actor, actor, EC_P2_OP_IDENTITY_READ);
        EcPhase2QueryRequest request{};
        request.meta.capability = scope.meta.capability;
        request.target = scope.target;
        EcPhase2IdentitySnapshot out{};
        std::array<char, 129> name{};
        EcUtf8Buffer buffer{name.data(), name.size(), 0};
        require(client.identity(request, out, buffer) == EC_OK && std::string(name.data()) == names[actor],
                "SDK identity snapshot");
        return out;
    }
    EcPhase2RoleSnapshot roles(User actor) {
        const auto scope = scoped(actor, actor, EC_P2_OP_ROLE_READ);
        EcPhase2QueryRequest request{};
        request.meta.capability = scope.meta.capability;
        request.target = scope.target;
        EcPhase2RoleSnapshot out{};
        require(client.roles(request, out) == EC_OK, "SDK role snapshot");
        return out;
    }
    EcPhase2PermissionDecision permission(User actor, std::uint32_t action, std::uint32_t which = 0) {
        const auto scope = scoped(actor, actor, EC_P2_OP_PERMISSION_CHECK, which);
        EcPhase2PermissionRequest request{};
        request.meta.capability = scope.meta.capability;
        request.subject = request.target = scope.target;
        request.operation = action;
        request.asset = which;
        EcPhase2PermissionDecision out{};
        require(client.permission(request, out) == EC_OK, "SDK permission decision");
        return out;
    }
    EcPhase2AssetSnapshot balance(User actor, std::uint32_t which = EC_P2_ASSET_MONEY) {
        const auto scope = scoped(actor, actor, EC_P2_OP_ASSET_READ, which);
        EcPhase2AssetRequest request{};
        request.meta.capability = scope.meta.capability;
        request.target = scope.target;
        request.asset = which;
        EcPhase2AssetSnapshot out{};
        require(client.asset(request, out) == EC_OK, "SDK balance snapshot");
        return out;
    }
    EcPhase2Receipt receipt(User actor, EcReceiptId id) {
        const auto scope = scoped(actor, actor, EC_P2_OP_RECEIPT_READ);
        EcPhase2ReceiptRequest request{};
        request.meta.capability = scope.meta.capability;
        request.receipt_id = id;
        EcPhase2Receipt out{};
        require(client.receipt(request, out) == EC_OK, "SDK durable receipt lookup");
        return out;
    }
    Json outbox(User actor) {
        const auto scope = scoped(actor, actor, EC_P2_OP_OUTBOX_READ);
        EcPhase2OutboxRequest request{};
        request.meta.capability = scope.meta.capability;
        request.limit = 100;
        std::array<EcPhase2OutboxEvent, 100> storage{};
        EcPhase2OutboxBuffer out{};
        out.data = storage.data();
        out.capacity = storage.size();
        require(client.outbox(request, out) == EC_OK, "SDK retained Outbox history");
        Json rows = Json::array();
        for (std::uint32_t i = 0; i < out.count; ++i) {
            const auto &event = storage[i];
            rows.push_back(
                Json::array({event.event_id, hex(event.receipt_id), hex(event.request_id), hex(event.target),
                             event.type, event.asset, event.authoritative_balance, event.account_revision,
                             event.occurred_at_unix_ms}));
        }
        return rows;
    }
};

void test(const char *name, const std::function<void()> &run, unsigned &groups) {
    run();
    ++groups;
    std::cout << "PASS SYNTHETIC_INTEGRATION " << name << '\n';
}

void parallelDomain(Harness &fixture) {
    using namespace eternal::core;
    const auto path = fixture.root / "parallel-domain.sqlite3";
    const auto identity = [&](User u) {
        return VerifiedIdentity{uuid(fixture.users[u].identity.trusted_uuid),
                                std::to_string(fixture.users[u].identity.trusted_xuid), names[u]};
    };
    {
        Core seed(path.string(), ownerXuid);
        const auto owner = *seed.ensurePlayer(identity(Owner)).actor;
        const auto a = *seed.ensurePlayer(identity(PlayerA)).actor,
                   b = *seed.ensurePlayer(identity(PlayerB)).actor;
        require(seed.ownerGrant(owner, a.uuid(), Asset::Coin, 1000, "parallel seed", "seed-a").status ==
                    Status::Ok,
                "Parallel A seed");
        require(seed.ownerGrant(owner, b.uuid(), Asset::Coin, 1000, "parallel seed", "seed-b").status ==
                    Status::Ok,
                "Parallel B seed");
    }
    Core first(path.string(), ownerXuid), second(path.string(), ownerXuid);
    const auto a = *first.restoreActor(identity(PlayerA).uuid).actor,
               b = *second.restoreActor(identity(PlayerB).uuid).actor;
    Result answerA, answerB;
    std::exception_ptr errorA, errorB;
    std::barrier ready(3);
    // Genuine two-connection/two-thread DOMAIN execution. Runtime is never called here.
    std::thread threadA([&] {
        ready.arrive_and_wait();
        try {
            answerA = first.transfer(a, b.uuid(), 175, "parallel exchange", "parallel-key");
        } catch (...) {
            errorA = std::current_exception();
        }
    });
    std::thread threadB([&] {
        ready.arrive_and_wait();
        try {
            answerB = second.transfer(b, a.uuid(), 75, "parallel exchange", "parallel-key");
        } catch (...) {
            errorB = std::current_exception();
        }
    });
    ready.arrive_and_wait();
    threadA.join();
    threadB.join();
    if (errorA)
        std::rethrow_exception(errorA);
    if (errorB)
        std::rethrow_exception(errorB);
    // SQLITE_BUSY is a retryable refusal; retry the identical actor/key/payload.
    if (answerA.status == Status::Busy)
        answerA = first.transfer(a, b.uuid(), 175, "parallel exchange", "parallel-key");
    if (answerB.status == Status::Busy)
        answerB = second.transfer(b, a.uuid(), 75, "parallel exchange", "parallel-key");
    require(answerA.status == Status::Ok && answerB.status == Status::Ok, "Parallel domain commit");
    require(first.balance(a.uuid()).amount == 900 && first.balance(b.uuid()).amount == 1100,
            "Parallel exact balances");
    require(first.balance(a.uuid()).amount + first.balance(b.uuid()).amount == 2000,
            "Parallel money conservation");
    const auto replayA = first.transfer(a, b.uuid(), 175, "parallel exchange", "parallel-key");
    const auto replayB = second.transfer(b, a.uuid(), 75, "parallel exchange", "parallel-key");
    require(replayA.replayed && replayB.replayed && replayA.receipt == answerA.receipt &&
                replayB.receipt == answerB.receipt,
            "Parallel actor-scoped receipts");
    ReadDb db(path);
    db.consistent();
    require(db.count("SELECT count(*) FROM transactions WHERE idempotencyKey='parallel-key'") == 2,
            "Same key must be isolated per actor");
    require(db.count("SELECT count(*) FROM entries e JOIN transactions t ON t.transactionId=e.transactionId "
                     "WHERE t.idempotencyKey='parallel-key'") == 4,
            "Parallel debit/credit ledger pairs");
    require(db.count("SELECT sum(delta) FROM entries e JOIN transactions t ON "
                     "t.transactionId=e.transactionId WHERE t.idempotencyKey='parallel-key'") == 0,
            "Parallel transfer ledger conservation");
    std::cout << "DETAIL SYNTHETIC_INTEGRATION DOMAIN two SQLite connections / two simultaneous threads; "
                 "Runtime concurrency is server-thread serialization\n";
}
} // namespace

int main() {
    try {
        unsigned groups{};
        test(
            "eight unique authenticated identities and role-permission matrix",
            [] {
                Harness h;
                // Configured Owner has every effective Role; other masks are explicit assignments.
                const std::array<std::uint64_t, UserCount> assigned{EC_P2_ROLE_ALL,
                                                                    EC_P2_ROLE_ADMINISTRATOR,
                                                                    EC_P2_ROLE_BUILDER,
                                                                    EC_P2_ROLE_MODERATOR,
                                                                    0,
                                                                    0,
                                                                    0,
                                                                    0};
                for (std::size_t i = 0; i < UserCount; ++i) {
                    const auto u = static_cast<User>(i);
                    const auto snapshot = h.identity(u);
                    const auto role = h.roles(u);
                    require(snapshot.xuid == 700000 + i &&
                                std::memcmp(&snapshot.uuid, &h.users[i].identity.trusted_uuid,
                                            sizeof(EcUuid)) == 0,
                            "Stable authenticated identifiers");
                    require(snapshot.first_seen_unix_ms > 0 && snapshot.identity_revision == 1 &&
                                snapshot.role_revision == role.role_revision,
                            "Identity role revisions");
                    require(role.role_mask == (EC_P2_ROLE_PLAYER | assigned[i]),
                            "Exact synthetic role assignment");
                    for (std::size_t j = 0; j < i; ++j)
                        require(!same(snapshot.player_id, h.users[j].joined.player_id), "Duplicate PlayerId");
                    require(h.permission(u, EC_P2_OP_TRANSFER, EC_P2_ASSET_MONEY).allowed == 1,
                            "Every Player can transfer own money");
                    require(h.permission(u, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY).allowed ==
                                (i == Owner || i == Administrator),
                            "Money adjustment matrix");
                    require(h.permission(u, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_REPUTATION).allowed ==
                                (i == Owner || i == Administrator),
                            "Reputation least privilege matrix");
                    require(h.permission(u, EC_P2_OP_ROLE_GRANT).allowed == (i == Owner),
                            "Only configured Owner manages Roles");
                }
                ReadDb(h.dbPath()).consistent();
            },
            groups);
        test(
            "OP-only is not Role and a Role does not require OP",
            [] {
                Harness h;
                require(h.users[OpOnly].nativeOp && !h.users[Builder].nativeOp,
                        "Synthetic engine OP fixture");
                const auto opBefore = h.roles(OpOnly), builderBefore = h.roles(Builder);
                require(opBefore.role_mask == EC_P2_ROLE_PLAYER &&
                            (builderBefore.role_mask & EC_P2_ROLE_BUILDER),
                        "OP independent role assignments");
                require(h.asset(OpOnly, OpOnly, EC_P2_ASSET_MONEY, 1).status == EC_DENIED,
                        "OP cannot adjust money");
                h.users[OpOnly].nativeOp = false;
                h.users[Builder].nativeOp = true;
                require(h.roles(OpOnly).role_mask == opBefore.role_mask &&
                            h.roles(OpOnly).role_revision == opBefore.role_revision,
                        "Removing OP cannot remove/grant Role");
                require(h.roles(Builder).role_mask == builderBefore.role_mask &&
                            h.roles(Builder).role_revision == builderBefore.role_revision,
                        "Adding OP cannot change Role");
                require(!(h.identity(OpOnly).flags & EC_P2_IDENTITY_OWNER),
                        "OP does not confer configured Owner");
            },
            groups);
        test(
            "ordinary high-value denial has durable receipt without financial effects",
            [] {
                Harness h;
                ReadDb db(h.dbPath());
                const auto entries = db.count("SELECT count(*) FROM entries"),
                           events = db.count("SELECT count(*) FROM outbox");
                for (User u : {Builder, Moderator, PlayerA, PlayerB, OpOnly, Revoked}) {
                    const auto result = h.asset(u, u, EC_P2_ASSET_MONEY, 100);
                    require(result.status == EC_DENIED && result.submission.state == EC_RECEIPT_REJECTED &&
                                nonzero(result.submission.receipt_id),
                            "Durable ordinary denial");
                    const auto receipt = h.receipt(u, result.submission.receipt_id);
                    require(receipt.state == EC_RECEIPT_REJECTED && receipt.result == EC_DENIED &&
                                h.balance(u).minor_units == 0,
                            "Denied receipt and balance");
                }
                require(db.count("SELECT count(*) FROM entries") == entries &&
                            db.count("SELECT count(*) FROM outbox") == events,
                        "Denied ledger/outbox side effects");
                db.consistent();
            },
            groups);
        test(
            "Owner grants/revokes another actor and stale capabilities are rejected",
            [] {
                Harness h;
                const auto before = h.roles(PlayerA);
                h.setRole(PlayerA, EC_P2_ROLE_ECONOMY_MANAGER, true);
                require(h.roles(PlayerA).role_revision == before.role_revision + 1 &&
                            h.permission(PlayerA, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY).allowed,
                        "Owner grant effective");
                auto stale = h.scoped(PlayerA, PlayerA, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100);
                h.setRole(PlayerA, EC_P2_ROLE_ECONOMY_MANAGER, false);
                const auto denied = h.submit(stale);
                require(denied.status == EC_REVOKED && nonzero(denied.submission.receipt_id) &&
                            h.balance(PlayerA).minor_units == 0,
                        "Stale privileged request persisted rejection");
                require(h.roles(PlayerA).role_revision == before.role_revision + 2 &&
                            !h.permission(PlayerA, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY).allowed,
                        "Owner revoke effective");
                const auto revoked = h.submit(h.revokedRequest);
                require(h.roles(Revoked).role_mask == EC_P2_ROLE_PLAYER &&
                            h.roles(Revoked).role_revision == 3 && revoked.status == EC_REVOKED &&
                            nonzero(revoked.submission.receipt_id),
                        "Named RevokedPlayer retains revised role and rejects its old signed request");
                auto forbidden =
                    h.submit(h.scoped(Owner, PlayerA, EC_P2_OP_ROLE_GRANT, 0, 0, {}, EC_P2_ROLE_OWNER));
                require(forbidden.status == EC_DENIED && nonzero(forbidden.submission.receipt_id),
                        "Owner cannot be granted by Role API");
                ReadDb(h.dbPath()).consistent();
            },
            groups);
        test(
            "forged context target subject and TTL fail without half commits",
            [] {
                Harness h;
                auto forged = h.scoped(Owner, Owner, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100);
                forged.meta.caller_context = {0xdead, 0xbeef};
                auto out = dto<EcPhase2Submission>();
                require(h.binding.scoped_api->submit_mutation(&forged, &out) == EC_DENIED,
                        "Raw SDK rejects forged context");
                auto scoped = h.scoped(Owner, Owner, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100);
                scoped.target = h.users[PlayerB].joined.player_id;
                require(h.submit(scoped).status == EC_DENIED, "Capability cannot change target");
                auto expired = h.scoped(Owner, Owner, EC_P2_OP_ASSET_ADD, EC_P2_ASSET_MONEY, 100);
                h.clock += 60001;
                const auto denied = h.submit(expired);
                require(denied.status == EC_EXPIRED && nonzero(denied.submission.receipt_id),
                        "TTL denial has durable receipt");
                const auto checkScope =
                    h.scoped(PlayerA, PlayerA, EC_P2_OP_PERMISSION_CHECK, EC_P2_ASSET_MONEY);
                EcPhase2PermissionRequest check{};
                check.meta.capability = checkScope.meta.capability;
                check.target = checkScope.target;
                check.subject = h.users[Owner].joined.player_id;
                check.operation = EC_P2_OP_ASSET_ADD;
                check.asset = EC_P2_ASSET_MONEY;
                EcPhase2PermissionDecision decision{};
                require(h.client.permission(check, decision) == EC_DENIED,
                        "Cannot self-report Owner subject");
                require(h.balance(Owner).minor_units == 0 && h.balance(PlayerB).minor_units == 0,
                        "Rejected scope changed balances");
                ReadDb(h.dbPath()).consistent();
            },
            groups);
        test(
            "disconnect reconnect invalidates old session and delayed disconnect",
            [] {
                Harness h;
                const auto oldIdentity = h.identity(PlayerB);
                const auto oldJoin = h.users[PlayerB].joined;
                const auto scope = h.scoped(PlayerB, PlayerB, EC_P2_OP_ASSET_READ, EC_P2_ASSET_MONEY);
                require(h.disconnect(PlayerB) == EC_OK, "Synthetic disconnect");
                h.authenticate(PlayerB);
                const auto now = h.identity(PlayerB);
                require(same(oldIdentity.player_id, now.player_id) &&
                            oldIdentity.first_seen_unix_ms == now.first_seen_unix_ms &&
                            now.session_generation == oldJoin.session_generation + 1,
                        "Reconnect persistent identity and new generation");
                EcPhase2AssetRequest request{};
                request.meta.capability = scope.meta.capability;
                request.target = scope.target;
                request.asset = EC_P2_ASSET_MONEY;
                EcPhase2AssetSnapshot out{};
                require(h.client.asset(request, out) == EC_REVOKED, "Old session capability revoked");
                require(h.disconnect(PlayerB, oldJoin.session_generation) == EC_CONFLICT &&
                            h.balance(PlayerB).minor_units == 0,
                        "Late disconnect cannot revoke reconnection");
            },
            groups);
        test(
            "A to B transfer replay conflict insufficient funds and reconciliation",
            [] {
                Harness h;
                require(h.asset(Owner, PlayerA, EC_P2_ASSET_MONEY, 1000).status == EC_OK, "Seed A");
                const auto key = h.key();
                const auto paid = h.transfer(PlayerA, PlayerB, 250, key);
                require(paid.status == EC_OK && h.balance(PlayerA).minor_units == 750 &&
                            h.balance(PlayerB).minor_units == 250,
                        "Transfer debit and credit");
                const auto replay = h.transfer(PlayerA, PlayerB, 250, key);
                require(replay.status == EC_OK && (replay.submission.flags & EC_P2_SUBMISSION_REPLAYED) &&
                            same(paid.submission.receipt_id, replay.submission.receipt_id),
                        "Exactly one transfer for same actor/key/payload");
                require(h.transfer(PlayerA, PlayerB, 251, key).status == EC_CONFLICT,
                        "Changed payload conflict");
                const auto deniedKey = h.key();
                const auto denied = h.transfer(PlayerA, PlayerB, 10000, deniedKey);
                require(denied.status == EC_INSUFFICIENT_COIN && nonzero(denied.submission.receipt_id),
                        "Insufficient transfer durable rejection");
                require(h.transfer(PlayerA, PlayerB, 10000, deniedKey).status == EC_INSUFFICIENT_COIN,
                        "Rejected key replays rejection");
                const auto receipt = h.receipt(PlayerA, denied.submission.receipt_id);
                require(receipt.state == EC_RECEIPT_REJECTED && receipt.target_balance == 750 &&
                            receipt.recipient_balance == 250,
                        "Denied receipt original reconciled accounts");
                require(h.balance(PlayerA).minor_units + h.balance(PlayerB).minor_units == 1000,
                        "Transfer money conservation");
                require(h.outbox(PlayerA).size() == 2 && h.outbox(PlayerB).size() == 1,
                        "Outbox own actor filtering");
                ReadDb(h.dbPath()).consistent();
            },
            groups);
        test(
            "two queued identities use server-thread serialization",
            [] {
                Harness h;
                require(h.asset(Owner, PlayerA, EC_P2_ASSET_MONEY, 1000).status == EC_OK, "Queue A seed");
                require(h.asset(Owner, PlayerB, EC_P2_ASSET_MONEY, 1000).status == EC_OK, "Queue B seed");
                // Both intents exist before either executes; Runtime still has one service thread.
                std::array<EcPhase2MutationRequest, 2> queue{
                    h.scoped(PlayerA, PlayerA, EC_P2_OP_TRANSFER, EC_P2_ASSET_MONEY, 200, {}, 0,
                             h.users[PlayerB].joined.player_id),
                    h.scoped(PlayerB, PlayerB, EC_P2_OP_TRANSFER, EC_P2_ASSET_MONEY, 350, {}, 0,
                             h.users[PlayerA].joined.player_id)};
                for (const auto &request : queue)
                    require(h.submit(request).status == EC_OK, "Serialized queued request");
                require(h.balance(PlayerA).minor_units == 1150 && h.balance(PlayerB).minor_units == 850,
                        "Queued exact debit/credit balances");
                ReadDb(h.dbPath()).consistent();
            },
            groups);
        test(
            "DOMAIN genuine concurrent opposite transfers on two connections",
            [] {
                Harness h;
                parallelDomain(h);
            },
            groups);
        test(
            "all eight identities assets receipts ledger and Outbox survive full restart",
            [] {
                Harness h;
                for (std::size_t i = 0; i < UserCount; ++i) {
                    require(h.asset(Owner, static_cast<User>(i), EC_P2_ASSET_MONEY,
                                    1000 + static_cast<std::int64_t>(i) * 100)
                                    .status == EC_OK,
                            "Restart money seed");
                    require(h.asset(Owner, static_cast<User>(i), EC_P2_ASSET_REPUTATION,
                                    10 + static_cast<std::int64_t>(i))
                                    .status == EC_OK,
                            "Restart reputation seed");
                }
                const auto paidKey = h.key(), rejectedKey = h.key();
                const auto paid = h.transfer(PlayerA, PlayerB, 230, paidKey),
                           rejected = h.transfer(PlayerA, PlayerB, 1000000, rejectedKey);
                require(paid.status == EC_OK && rejected.status == EC_INSUFFICIENT_COIN,
                        "Restart transaction seeds");
                const auto paidReceipt = h.receipt(PlayerA, paid.submission.receipt_id),
                           rejectedReceipt = h.receipt(PlayerA, rejected.submission.receipt_id);
                std::array<EcPhase2IdentitySnapshot, UserCount> identities{};
                std::array<EcPhase2RoleSnapshot, UserCount> roles{};
                std::array<EcPhase2AssetSnapshot, UserCount> money{}, reputation{};
                for (std::size_t i = 0; i < UserCount; ++i) {
                    const auto u = static_cast<User>(i);
                    identities[i] = h.identity(u);
                    roles[i] = h.roles(u);
                    money[i] = h.balance(u);
                    reputation[i] = h.balance(u, EC_P2_ASSET_REPUTATION);
                }
                const auto old = h.scoped(PlayerA, PlayerA, EC_P2_OP_ASSET_READ, EC_P2_ASSET_MONEY);
                const auto history = h.outbox(Owner);
                Json ledger, receipts, events;
                {
                    ReadDb db(h.dbPath());
                    db.consistent();
                    ledger = db.rows("SELECT * FROM entries ORDER BY id");
                    receipts = db.rows("SELECT * FROM receipts ORDER BY transactionId");
                    events = db.rows("SELECT * FROM outbox ORDER BY id");
                }
                for (std::size_t i = 0; i < UserCount; ++i)
                    require(h.disconnect(static_cast<User>(i)) == EC_OK, "Disconnect all synthetic sessions");
                h.stop(); // Destruction clears every process-local session/context/capability.
                h.start();
                for (std::size_t i = 0; i < UserCount; ++i) {
                    const auto u = static_cast<User>(i);
                    const auto after = h.identity(u);
                    const auto role = h.roles(u);
                    require(same(after.player_id, identities[i].player_id) &&
                                after.xuid == identities[i].xuid &&
                                std::memcmp(&after.uuid, &identities[i].uuid, sizeof(EcUuid)) == 0,
                            "Restart stable PlayerId/XUID/UUID");
                    require(after.first_seen_unix_ms == identities[i].first_seen_unix_ms &&
                                after.identity_revision == identities[i].identity_revision &&
                                after.last_seen_unix_ms >= identities[i].last_seen_unix_ms,
                            "Restart stable firstSeen identity revision");
                    require(role.role_mask == roles[i].role_mask &&
                                role.role_revision == roles[i].role_revision,
                            "Restart role permissions");
                    require(h.balance(u).minor_units == money[i].minor_units &&
                                h.balance(u).account_revision == money[i].account_revision,
                            "Restart Money");
                    require(h.balance(u, EC_P2_ASSET_REPUTATION).minor_units == reputation[i].minor_units &&
                                h.balance(u, EC_P2_ASSET_REPUTATION).account_revision ==
                                    reputation[i].account_revision,
                            "Restart Reputation");
                }
                EcPhase2AssetRequest stale{};
                stale.meta = old.meta;
                stale.meta.struct_size = sizeof(stale);
                stale.target = old.target;
                stale.asset = EC_P2_ASSET_MONEY;
                auto invalid = dto<EcPhase2AssetSnapshot>();
                require(h.binding.scoped_api->read_asset(&stale, &invalid) == EC_DENIED,
                        "Destroyed session/context does not survive Core restart");
                const auto replay = h.transfer(PlayerA, PlayerB, 230, paidKey),
                           replayRejected = h.transfer(PlayerA, PlayerB, 1000000, rejectedKey);
                require(replay.status == EC_OK && (replay.submission.flags & EC_P2_SUBMISSION_REPLAYED) &&
                            same(replay.submission.receipt_id, paid.submission.receipt_id),
                        "Reauthenticated retry recovers original commit");
                require(replayRejected.status == EC_INSUFFICIENT_COIN &&
                            same(replayRejected.submission.receipt_id, rejected.submission.receipt_id),
                        "Reauthenticated retry recovers original rejection");
                const auto restoredPaid = h.receipt(PlayerA, paid.submission.receipt_id),
                           restoredRejected = h.receipt(PlayerA, rejected.submission.receipt_id);
                require(same(restoredPaid.request_id, paidReceipt.request_id) &&
                            restoredPaid.completed_at_unix_ms == paidReceipt.completed_at_unix_ms &&
                            restoredPaid.target_balance == paidReceipt.target_balance,
                        "Persistent committed receipt metadata");
                require(restoredRejected.state == EC_RECEIPT_REJECTED &&
                            restoredRejected.result == rejectedReceipt.result &&
                            restoredRejected.completed_at_unix_ms == rejectedReceipt.completed_at_unix_ms,
                        "Persistent rejected receipt metadata");
                require(h.outbox(Owner) == history,
                        "Restart retained Outbox history and authoritative projection values");
                ReadDb db(h.dbPath());
                db.consistent();
                require(db.rows("SELECT * FROM entries ORDER BY id") == ledger &&
                            db.rows("SELECT * FROM receipts ORDER BY transactionId") == receipts &&
                            db.rows("SELECT * FROM outbox ORDER BY id") == events,
                        "Restart/retry duplicated ledger receipt or Outbox");
            },
            groups);
        std::cout << "PASS SYNTHETIC_INTEGRATION " << groups
                  << " multi-user groups; 8 synthetic identities; real BDS/client NOT RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL SYNTHETIC_INTEGRATION " << error.what() << '\n';
        return 1;
    }
}
