#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eternal::core {

enum class Asset { Coin, Reputation };
enum class Role {
    Build,
    Economy,
    Law,
    Content,
    Resources,
    Operations,
    Owner,
    Admin,
    ContentManager,
    EconomyManager,
    Builder,
    Moderator,
    Player
};
enum class Permission {
    IdentityRead,
    PermissionRead,
    RoleManage,
    MoneyRead,
    MoneyAdjust,
    MoneyTransfer,
    ReputationRead,
    ReputationAdjust,
    AuditRead,
    ContentManage,
    WorldBuild,
    Moderation,
    OperationsManage,
    ResourcesManage
};
enum class Status {
    Ok,
    Busy,
    Invalid,
    Overflow,
    PermissionDenied,
    Conflict,
    NotFound,
    InsufficientFunds,
    StorageError
};
enum class FaultPoint { AfterDebit, BeforeCommit, AfterCommit, DuringMigration };
using FaultHook = std::function<void(FaultPoint)>;

// Construct ONLY from authenticated server join/origin data in the native adapter.
// Never populate these fields from chat, packets, command arguments or a source name.
// XUID must be a nonzero canonical decimal uint64 value.
struct VerifiedIdentity {
    std::string uuid;
    std::string xuid;
    std::string displayName{};
};

class Actor {
  public:
    const std::string &uuid() const noexcept { return uuid_; }
    const std::string &xuid() const noexcept { return xuid_; }
    const std::string &playerId() const noexcept { return playerId_; }
    std::uint64_t identityVersion() const noexcept { return identityVersion_; }

  private:
    Actor(std::string uuid, std::string xuid, std::string playerId, std::uint64_t version);
    std::string uuid_;
    std::string xuid_;
    std::string playerId_;
    std::uint64_t identityVersion_{};
    friend class Core;
};

struct IdentityResult {
    Status status{Status::Invalid};
    std::optional<Actor> actor;
    bool created{};
    std::string error;
};

struct BalanceResult {
    Status status{Status::Invalid};
    std::int64_t amount{};
    std::string error;
    std::uint64_t revision{};
    std::uint64_t ledgerRevision{};
};

struct Result {
    Status status{Status::Invalid};
    // Exact durable JSON receipt; identical on every successful replay.
    std::string receipt;
    bool replayed{};
    std::string error;
};

struct OutboxEvent {
    std::int64_t id{};
    std::string txId;
    std::string targetUuid;
    std::string type;
    std::string payload;
    std::uint64_t retryCount{};
    std::int64_t nextAttemptAt{};
    std::string lastError;
    std::string deliveryStatus;
    std::int64_t createdAt{};
    std::string requestId{};
};

struct PlayerSnapshot {
    std::string playerId, uuid, xuid, displayName;
    std::int64_t firstSeen{}, lastSeen{};
    std::uint64_t identityVersion{}, permissionRevision{};
};
struct PlayerResult {
    Status status{Status::Invalid};
    std::optional<PlayerSnapshot> player;
    std::string error;
};
// Attribution is trusted runtime context, never an authorization grant.
struct DomainRequestMetadata {
    std::string moduleId{"core"};
    std::string action;
    std::string requestId;
    std::uint64_t permissionRevision{};
    // Sorted canonical request JSON built by Core runtime from parsed DTO values.
    // Excludes capability tokens, request IDs and idempotency keys. Empty retains
    // legacy internal encoding. This field never authorizes an operation.
    std::string canonicalRequestPayload{};
};
struct DenialRecord {
    std::optional<std::string> actorUuid;
    std::string moduleId, action, targetUuid, reason, requestId, transactionId;
    Status result{Status::PermissionDenied};
    std::uint64_t permissionRevision{};
};
struct Diagnostics {
    std::int64_t players{}, roles{}, transactions{}, ledgerEntries{}, audit{}, receipts{}, outbox{},
        pendingDeliveries{};
    bool ledgerConsistent{}, receiptsConsistent{};
};
struct DiagnosticResult {
    Status status{Status::Invalid};
    Diagnostics value;
    std::string error;
};

class Core {
  public:
    // Throws on an unusable DB, unsupported schema, or different persisted owner.
    // FaultHook is for deterministic tests only; production uses the default.
    explicit Core(std::string dbPath, std::string ownerXuid, FaultHook fault = {});
    ~Core();
    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;

    // Server-only registration primitive. Player commands have no create endpoint.
    IdentityResult ensurePlayer(const VerifiedIdentity &identity);
    // Private server-only lookup after authenticating an existing session. No writes.
    IdentityResult restoreActor(std::string_view playerIdOrUuid) const;
    PlayerResult player(std::string_view playerIdOrUuid) const;
    // Display-only selector: duplicated names return Conflict and grant no identity.
    PlayerResult findPlayerByDisplayName(std::string_view displayName) const;
    DiagnosticResult selfcheck() const;
    std::optional<std::uint64_t> permissionRevision(std::string_view uuid) const;
    BalanceResult balance(std::string_view uuid, Asset asset = Asset::Coin) const;
    bool hasRole(const Actor &actor, Role role) const;
    // Action capability only. Runtime must additionally enforce self/target scopes.
    bool hasPermission(const Actor &actor, Permission permission) const;

    // Default keys retain core.domain.v1 + actor UUID for legacy replay. Verified
    // runtime module metadata derives a separate fixed scope per registered module.
    // Request reasons and keys must contain valid UTF-8 without ASCII control bytes.
    Result setRole(const Actor &owner, std::string_view targetUuid, Role role, bool enabled,
                   std::string_view reason, std::string_view idempotencyKey,
                   const DomainRequestMetadata &metadata = {});
    Result ownerGrant(const Actor &owner, std::string_view targetUuid, Asset asset,
                      std::int64_t amount, std::string_view reason, std::string_view idempotencyKey,
                      const DomainRequestMetadata &metadata = {});
    Result adjustAsset(const Actor &actor, std::string_view targetUuid, Asset asset,
                       std::int64_t delta, std::string_view reason, std::string_view idempotencyKey,
                       const DomainRequestMetadata &metadata = {});
    // Coin transfer works for registered offline targets. Reputation is not transferable.
    Result transfer(const Actor &caller, std::string_view targetUuid, std::int64_t amount,
                    std::string_view reason, std::string_view idempotencyKey,
                    const DomainRequestMetadata &metadata = {});
    Result receipt(const Actor &actor, std::string_view idempotencyKey,
                   const DomainRequestMetadata &metadata = {}) const;
    Result receiptById(const Actor &actor, std::string_view transactionId,
                       const DomainRequestMetadata &metadata = {}) const;
    // Bounded detailed denial audit plus durable aggregation; unauthenticated actors
    // are represented as NULL, not a invented player identity.
    Status auditDenied(const DenialRecord &record);
    Result recordRejectedRequest(const Actor &actor, std::string_view targetUuid,
                                 std::string_view operation,
                                 std::string_view canonicalRequestPayload, std::string_view reason,
                                 std::string_view idempotencyKey, Status result,
                                 const DomainRequestMetadata &metadata = {});

    // Trusted server projection worker only: set projections to authoritative values,
    // then acknowledge. Neither method changes authoritative account balances.
    std::vector<OutboxEvent> pendingOutbox(std::size_t limit = 100) const;
    // Trusted runtime reads retained history by persistent cursor, then filters
    // authenticated target scopes. Reading does not acknowledge delivery.
    std::vector<OutboxEvent> eventsAfter(std::int64_t afterEventId, std::size_t limit = 100) const;
    Status acknowledgeOutbox(std::int64_t eventId);
    Status registerConsumer(std::string_view consumerId);
    // At-least-once projection. Read current Core balances, or reject stale event IDs;
    // acknowledgements deduplicate by (consumer,event). No physical item delivery.
    std::vector<OutboxEvent> outboxFor(std::string_view consumerId, std::size_t limit = 100) const;
    // Enriched at-least-once projection for operator-approved SDK consumers only. Same
    // delivery scope as outboxFor, but also exposes createdAt and request attribution.
    // outboxFor keeps its legacy metadata semantics unchanged.
    std::vector<OutboxEvent> consumerEvents(std::string_view consumerId, std::size_t limit = 100) const;
    Status recordOutboxAttempt(std::string_view consumerId, std::int64_t eventId,
                               std::string_view error, std::int64_t retryAfterMs,
                               bool targetOffline = false);
    Status acknowledgeOutbox(std::string_view consumerId, std::int64_t eventId);
    // SQLite online backup of this Native DB; destination must not exist.
    Status backup(std::string_view destination) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char *name(Status status) noexcept;
const char *name(Asset asset) noexcept;
const char *name(Role role) noexcept;
const char *name(Permission permission) noexcept;

} // namespace eternal::core
