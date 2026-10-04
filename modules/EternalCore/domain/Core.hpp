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
enum class Role { Build, Economy, Law, Content, Resources, Operations };
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
enum class FaultPoint { AfterDebit, BeforeCommit, AfterCommit };
using FaultHook = std::function<void(FaultPoint)>;

// Construct ONLY from authenticated server join/origin data in the native adapter.
// Never populate these fields from chat, packets, command arguments or a source name.
// XUID must be a nonzero canonical decimal uint64 value.
struct VerifiedIdentity {
    std::string uuid;
    std::string xuid;
};

class Actor {
  public:
    const std::string &uuid() const noexcept { return uuid_; }
    const std::string &xuid() const noexcept { return xuid_; }

  private:
    Actor(std::string uuid, std::string xuid);
    std::string uuid_;
    std::string xuid_;
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
    BalanceResult balance(std::string_view uuid, Asset asset = Asset::Coin) const;
    bool hasRole(const Actor &actor, Role role) const;

    // Keys belong to fixed core.domain.v1 + verified actor UUID, not a global namespace.
    // Request reasons and keys must contain valid UTF-8 without ASCII control bytes.
    Result setRole(const Actor &owner, std::string_view targetUuid, Role role, bool enabled,
                   std::string_view reason, std::string_view idempotencyKey);
    Result ownerGrant(const Actor &owner, std::string_view targetUuid, Asset asset,
                      std::int64_t amount, std::string_view reason,
                      std::string_view idempotencyKey);
    // Coin transfer works for registered offline targets. Reputation is not transferable.
    Result transfer(const Actor &caller, std::string_view targetUuid, std::int64_t amount,
                    std::string_view reason, std::string_view idempotencyKey);

    // Trusted server projection worker only: set projections to authoritative values,
    // then acknowledge. Neither method changes authoritative account balances.
    std::vector<OutboxEvent> pendingOutbox(std::size_t limit = 100) const;
    Status acknowledgeOutbox(std::int64_t eventId);
    // SQLite online backup of this Native DB; destination must not exist.
    Status backup(std::string_view destination) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char *name(Status status) noexcept;
const char *name(Asset asset) noexcept;
const char *name(Role role) noexcept;

} // namespace eternal::core
