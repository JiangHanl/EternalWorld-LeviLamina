#include "Core.hpp"
#include "Schema.hpp"
#include "Sha256.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <utility>

namespace eternal::core {
namespace {
constexpr std::string_view executorScope = "core.domain.v1";
struct SqlError : std::runtime_error {
    int code;
    SqlError(int c, const std::string &message) : runtime_error(message), code(c) {}
};
void check(sqlite3 *db, int rc) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw SqlError(rc, sqlite3_errmsg(db));
}
void execute(sqlite3 *db, const char *sql) {
    check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}
struct Statement {
    sqlite3 *db;
    sqlite3_stmt *s{};
    Statement(sqlite3 *d, const char *sql) : db(d) {
        check(db, sqlite3_prepare_v2(db, sql, -1, &s, nullptr));
    }
    ~Statement() { sqlite3_finalize(s); }
    void text(int index, std::string_view v) {
        check(db, sqlite3_bind_text(s, index, v.empty() ? "" : v.data(), static_cast<int>(v.size()),
                                    SQLITE_TRANSIENT));
    }
    void number(int index, std::int64_t v) { check(db, sqlite3_bind_int64(s, index, v)); }
    void null(int index) { check(db, sqlite3_bind_null(s, index)); }
    bool row() {
        int rc = sqlite3_step(s);
        check(db, rc);
        return rc == SQLITE_ROW;
    }
    void done() {
        if (row())
            throw SqlError(SQLITE_MISUSE, "Unexpected SQL result");
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(s, column); }
    std::string string(int column) const {
        const auto *p = sqlite3_column_text(s, column);
        return p ? std::string(reinterpret_cast<const char *>(p), sqlite3_column_bytes(s, column))
                 : std::string{};
    }
};
struct Transaction {
    sqlite3 *db;
    bool active{true};
    explicit Transaction(sqlite3 *d) : db(d) { execute(db, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (active)
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        execute(db, "COMMIT");
        active = false;
    }
};
std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
Status sqlStatus(const SqlError &e) {
    int base = e.code & 255;
    return base == SQLITE_BUSY || base == SQLITE_LOCKED ? Status::Busy
           : base == SQLITE_TOOBIG                      ? Status::Overflow
           : base == SQLITE_CONSTRAINT                  ? Status::Conflict
                                                        : Status::StorageError;
}
bool validXuid(std::string_view x) {
    if (x.empty() || x.size() > 20 || x.front() == '0' ||
        !std::all_of(x.begin(), x.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    std::uint64_t parsed{};
    const auto result = std::from_chars(x.data(), x.data() + x.size(), parsed);
    return result.ec == std::errc{} && result.ptr == x.data() + x.size();
}
bool validUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
            continue;
        std::uint32_t codepoint, minimum;
        std::size_t following;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f;
            minimum = 0x80;
            following = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f;
            minimum = 0x800;
            following = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07;
            minimum = 0x10000;
            following = 3;
        } else
            return false;
        if (following > text.size() - i)
            return false;
        for (std::size_t n = 0; n < following; ++n) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
    }
    return true;
}
std::optional<std::string> uuid(std::string_view value) {
    if (value.size() != 36)
        return {};
    std::string out(value);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return {};
        } else if (c >= 'A' && c <= 'F')
            out[i] = static_cast<char>(c + 32);
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return {};
    }
    return out;
}
bool textValid(std::string_view text, std::size_t max) {
    return !text.empty() && text.size() <= max && validUtf8(text) &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
bool reasonValid(std::string_view reason) {
    return textValid(reason, 1024) && reason.find_first_not_of(' ') != std::string_view::npos;
}
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out + '\"';
}
std::string fields(std::initializer_list<std::string_view> values) {
    std::string out = "eternal-core-request-v1|";
    for (auto value : values) {
        out += std::to_string(value.size());
        out += ':';
        out.append(value);
        out += '|';
    }
    return out;
}
bool playerExists(sqlite3 *db, std::string_view id) {
    Statement q(db, "SELECT 1 FROM players WHERE uuid=?");
    q.text(1, id);
    return q.row();
}
std::int64_t amount(sqlite3 *db, std::string_view id, Asset asset) {
    Statement q(db, "SELECT balance FROM accounts WHERE uuid=? AND asset=?");
    q.text(1, id);
    q.text(2, name(asset));
    if (!q.row())
        throw SqlError(SQLITE_CORRUPT, "Account missing");
    return q.integer(0);
}
std::int64_t accountRevision(sqlite3 *db, std::string_view id, Asset asset) {
    Statement q(db, "SELECT revision FROM accounts WHERE uuid=? AND asset=?");
    q.text(1, id);
    q.text(2, name(asset));
    if (!q.row())
        throw SqlError(SQLITE_CORRUPT, "Account missing");
    return q.integer(0);
}
void writeBalance(sqlite3 *db, std::string_view id, Asset asset, std::int64_t value) {
    Statement q(db, "UPDATE accounts SET balance=?,revision=revision+1 WHERE uuid=? AND asset=? "
                    "AND revision<?");
    q.number(1, value);
    q.text(2, id);
    q.text(3, name(asset));
    q.number(4, std::numeric_limits<std::int64_t>::max());
    q.done();
    if (sqlite3_changes(db) != 1)
        throw SqlError(SQLITE_TOOBIG, "Account revision exhausted");
}
bool authenticated(sqlite3 *db, const Actor &actor) {
    Statement q(
        db, "SELECT 1 FROM players WHERE uuid=? AND xuid=? AND playerId=? AND identityVersion=?");
    q.text(1, actor.uuid());
    q.text(2, actor.xuid());
    q.text(3, actor.playerId());
    q.number(4, static_cast<std::int64_t>(actor.identityVersion()));
    return q.row();
}
bool assetValid(Asset a) { return a == Asset::Coin || a == Asset::Reputation; }
bool roleValid(Role r) { return r >= Role::Build && r <= Role::Player; }
bool metadataValid(const DomainRequestMetadata &metadata) {
    return textValid(metadata.moduleId, 80) &&
           (metadata.action.empty() || textValid(metadata.action, 80)) &&
           (metadata.requestId.empty() || textValid(metadata.requestId, 160)) &&
           (metadata.canonicalRequestPayload.empty() ||
            textValid(metadata.canonicalRequestPayload, 16384)) &&
           metadata.permissionRevision <=
               static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
std::string attributionAction(const DomainRequestMetadata &m, std::string_view op) {
    return m.action.empty() ? std::string(op) : m.action;
}
std::string requestScope(const DomainRequestMetadata &m) {
    return m.moduleId == "core" ? std::string(executorScope)
                                : "core.module." + detail::sha256(m.moduleId);
}
std::string requestPayload(std::string legacy, const DomainRequestMetadata &metadata) {
    if (!metadata.canonicalRequestPayload.empty())
        return metadata.canonicalRequestPayload;
    if (metadata.moduleId != "core" || !metadata.action.empty())
        legacy += fields({"attribution", metadata.moduleId, metadata.action});
    return legacy;
}
std::string randomId(sqlite3 *db) {
    Statement q(db, "SELECT lower(hex(randomblob(16)))");
    q.row();
    return "tx_" + q.string(0);
}
void verifyDatabase(sqlite3 *db) {
    Statement integrity(db, "PRAGMA integrity_check");
    if (!integrity.row() || integrity.string(0) != "ok" || integrity.row())
        throw SqlError(SQLITE_CORRUPT, "Database integrity_check failed");
    Statement foreign(db, "PRAGMA foreign_key_check");
    if (foreign.row())
        throw SqlError(SQLITE_CORRUPT, "Database foreign_key_check failed");
}
} // namespace

const char *name(Status s) noexcept {
    switch (s) {
    case Status::Ok:
        return "ok";
    case Status::Busy:
        return "busy";
    case Status::Invalid:
        return "invalid";
    case Status::Overflow:
        return "overflow";
    case Status::PermissionDenied:
        return "permission_denied";
    case Status::Conflict:
        return "conflict";
    case Status::NotFound:
        return "not_found";
    case Status::InsufficientFunds:
        return "insufficient_funds";
    default:
        return "storage_error";
    }
}
const char *name(Asset a) noexcept {
    return a == Asset::Coin ? "coin" : a == Asset::Reputation ? "reputation" : "invalid";
}
const char *name(Role r) noexcept {
    switch (r) {
    case Role::Build:
        return "build";
    case Role::Economy:
        return "economy";
    case Role::Law:
        return "law";
    case Role::Content:
        return "content";
    case Role::Resources:
        return "resources";
    case Role::Operations:
        return "operations";
    case Role::Owner:
        return "owner";
    case Role::Admin:
        return "admin";
    case Role::ContentManager:
        return "content_manager";
    case Role::EconomyManager:
        return "economy_manager";
    case Role::Builder:
        return "builder";
    case Role::Moderator:
        return "moderator";
    case Role::Player:
        return "player";
    default:
        return "invalid";
    }
}
const char *name(Permission p) noexcept {
    switch (p) {
    case Permission::IdentityRead:
        return "identity.read";
    case Permission::PermissionRead:
        return "permission.read";
    case Permission::RoleManage:
        return "role.manage";
    case Permission::MoneyRead:
        return "money.read";
    case Permission::MoneyAdjust:
        return "money.adjust";
    case Permission::MoneyTransfer:
        return "money.transfer";
    case Permission::ReputationRead:
        return "reputation.read";
    case Permission::ReputationAdjust:
        return "reputation.adjust";
    case Permission::AuditRead:
        return "audit.read";
    case Permission::ContentManage:
        return "content.manage";
    case Permission::WorldBuild:
        return "world.build";
    case Permission::Moderation:
        return "moderation";
    case Permission::OperationsManage:
        return "operations.manage";
    case Permission::ResourcesManage:
        return "resources.manage";
    default:
        return "invalid";
    }
}
Actor::Actor(std::string u, std::string x, std::string p, std::uint64_t v)
    : uuid_(std::move(u)), xuid_(std::move(x)), playerId_(std::move(p)), identityVersion_(v) {}

struct Core::Impl {
    sqlite3 *db{};
    std::string path, owner;
    FaultHook fault;
    mutable std::mutex mutex;
    Impl(std::string p, std::string o, FaultHook f)
        : path(std::move(p)), owner(std::move(o)), fault(std::move(f)) {
        if (path.empty() || !validXuid(owner))
            throw std::invalid_argument("Invalid DB path or owner XUID");
        const bool existed = std::filesystem::exists(path);
        int rc = sqlite3_open_v2(path.c_str(), &db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            std::string message = db ? sqlite3_errmsg(db) : "SQLite open failed";
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw SqlError(rc, message);
        }
        try {
            check(db, sqlite3_busy_timeout(db, 100));
            execute(db, "PRAGMA foreign_keys=ON");
            int version;
            {
                Statement q(db, "PRAGMA user_version");
                q.row();
                version = static_cast<int>(q.integer(0));
            }
            if (version > detail::schemaVersion)
                throw std::runtime_error("Unsupported newer schema");
            if (version == 0) {
                {
                    Statement q(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND "
                                    "name NOT LIKE 'sqlite_%'");
                    q.row();
                    if (q.integer(0) != 0)
                        throw std::runtime_error(
                            "Refusing unrecognized database; only Native schema may be migrated");
                }
            } else {
                {
                    Statement q(db, "SELECT value FROM metadata WHERE key='ownerXuid'");
                    if (!q.row() || q.string(0) != owner)
                        throw std::runtime_error(
                            "Persisted owner XUID mismatch; explicit owner migration required");
                }
                {
                    Statement q(db, "SELECT checksum FROM schema_migration WHERE version=1");
                    if (!q.row() || q.string(0) != detail::sha256(detail::schemaV1))
                        throw std::runtime_error("Schema migration checksum mismatch");
                }
                if (version >= 2) {
                    Statement q(db, "SELECT checksum FROM schema_migration WHERE version=2");
                    if (!q.row() || q.string(0) != detail::sha256(detail::schemaV2))
                        throw std::runtime_error("Schema migration checksum mismatch");
                }
                verifyDatabase(db);
            }
            {
                Statement q(db, "PRAGMA journal_mode=WAL");
                if (!q.row() || q.string(0) != "wal")
                    throw SqlError(SQLITE_ERROR, "WAL unavailable");
            }
            execute(db, "PRAGMA synchronous=FULL");
            if (version < detail::schemaVersion) {
                if (existed)
                    onlineBackup(path + ".pre-migration-" + (version == 0 ? "001-" : "002-") +
                                 std::to_string(now()) + ".sqlite");
                // Rebuild CHECK constraints without cascading existing child rows.
                // This connection is not exposed before migration has finished.
                execute(db, "PRAGMA foreign_keys=OFF");
                Transaction tx(db);
                if (version == 0) {
                    execute(db, std::string(detail::schemaV1).c_str());
                    Statement meta(db, "INSERT INTO metadata(key,value) VALUES('ownerXuid',?)");
                    meta.text(1, owner);
                    meta.done();
                    Statement migration(
                        db,
                        "INSERT INTO schema_migration(version,checksum,appliedAt) VALUES(1,?,?)");
                    migration.text(1, detail::sha256(detail::schemaV1));
                    migration.number(2, now());
                    migration.done();
                }
                execute(db, std::string(detail::schemaV2).c_str());
                Statement migration(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) VALUES(2,?,?)");
                migration.text(1, detail::sha256(detail::schemaV2));
                migration.number(2, now());
                migration.done();
                verifyDatabase(db);
                inject(FaultPoint::DuringMigration);
                tx.commit();
                execute(db, "PRAGMA foreign_keys=ON");
            }
            {
                Statement q(db, "SELECT value FROM metadata WHERE key='ownerXuid'");
                if (!q.row() || q.string(0) != owner)
                    throw std::runtime_error(
                        "Persisted owner XUID mismatch; explicit owner migration required");
            }
            {
                Statement q(db, "SELECT checksum FROM schema_migration WHERE version=2");
                if (!q.row() || q.string(0) != detail::sha256(detail::schemaV2))
                    throw std::runtime_error("Schema migration checksum mismatch");
            }
            verifyDatabase(db);
        } catch (...) {
            sqlite3_close(db);
            db = nullptr;
            throw;
        }
    }
    ~Impl() {
        if (db)
            sqlite3_close(db);
    }
    void onlineBackup(const std::string &destination) const {
        if (!textValid(destination, 4096) || std::filesystem::exists(destination) ||
            std::filesystem::exists(destination + ".sha256"))
            throw std::invalid_argument("Backup destination must be new");
        sqlite3 *target{};
        int rc = sqlite3_open_v2(destination.c_str(), &target,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            const auto message =
                target ? std::string(sqlite3_errmsg(target)) : std::string("Backup open failed");
            if (target)
                sqlite3_close(target);
            throw SqlError(rc, message);
        }
        sqlite3_backup *backup = sqlite3_backup_init(target, "main", db, "main");
        if (!backup) {
            rc = sqlite3_errcode(target);
            const std::string message = sqlite3_errmsg(target);
            sqlite3_close(target);
            throw SqlError(rc, message);
        }
        int retries = 0;
        do {
            rc = sqlite3_backup_step(backup, 128);
            if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                if (++retries > 100)
                    break;
                sqlite3_sleep(10);
            }
        } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
        int finish = sqlite3_backup_finish(backup);
        const std::string message = sqlite3_errmsg(target);
        if (rc != SQLITE_DONE || finish != SQLITE_OK) {
            sqlite3_close(target);
            throw SqlError(rc != SQLITE_DONE ? rc : finish, message);
        }
        try {
            verifyDatabase(target);
        } catch (...) {
            sqlite3_close(target);
            throw;
        }
        sqlite3_close(target);
        std::ifstream data(destination, std::ios::binary);
        if (!data)
            throw std::runtime_error("Cannot read backup checksum input");
        const std::string bytes((std::istreambuf_iterator<char>(data)),
                                std::istreambuf_iterator<char>());
        std::ofstream checksum(destination + ".sha256", std::ios::binary);
        if (!checksum)
            throw std::runtime_error("Cannot record backup checksum");
        checksum << detail::sha256(bytes) << "  "
                 << std::filesystem::path(destination).filename().string() << '\n';
        checksum.close();
        if (!checksum)
            throw std::runtime_error("Backup checksum write failed");
    }
    bool owns(const Actor &actor) const {
        return actor.xuid() == owner && authenticated(db, actor);
    }
    bool role(const Actor &actor, Role value) const {
        if (!roleValid(value) || !authenticated(db, actor))
            return false;
        if (value == Role::Player)
            return true;
        if (actor.xuid() == owner)
            return true;
        if (value == Role::Owner)
            return false;
        Statement q(db, "SELECT 1 FROM roles WHERE uuid=? AND role=?");
        q.text(1, actor.uuid());
        q.text(2, name(value));
        return q.row();
    }
    bool permits(const Actor &actor, Permission permission) const {
        if (permission < Permission::IdentityRead || permission > Permission::ResourcesManage ||
            !authenticated(db, actor))
            return false;
        if (owns(actor))
            return true;
        if (permission == Permission::RoleManage)
            return false;
        if (role(actor, Role::Admin))
            return true;
        switch (permission) {
        case Permission::IdentityRead:
        case Permission::PermissionRead:
        case Permission::MoneyRead:
        case Permission::MoneyTransfer:
        case Permission::ReputationRead:
            return true;
        case Permission::MoneyAdjust:
            return role(actor, Role::EconomyManager) || role(actor, Role::Economy);
        case Permission::ReputationAdjust:
            return false;
        case Permission::ContentManage:
            return role(actor, Role::ContentManager) || role(actor, Role::Content);
        case Permission::WorldBuild:
            return role(actor, Role::Builder) || role(actor, Role::Build);
        case Permission::Moderation:
            return role(actor, Role::Moderator) || role(actor, Role::Law);
        case Permission::OperationsManage:
            return role(actor, Role::Operations);
        case Permission::ResourcesManage:
            return role(actor, Role::Resources);
        case Permission::AuditRead:
            return role(actor, Role::Moderator) || role(actor, Role::Law) ||
                   role(actor, Role::EconomyManager) || role(actor, Role::Economy);
        default:
            return false;
        }
    }
    void inject(FaultPoint point) {
        if (fault)
            fault(point);
    }
    std::optional<Result> replay(const Actor &actor, std::string_view key, std::string_view hash,
                                 std::string_view scope = executorScope) {
        Statement q(
            db,
            "SELECT t.transactionId,t.requestHash,r.payload,t.resultCode FROM transactions AS t "
            "JOIN receipts AS r ON r.transactionId=t.transactionId "
            "WHERE t.executorScope=? AND t.actorUuid=? AND t.idempotencyKey=?");
        q.text(1, scope);
        q.text(2, actor.uuid());
        q.text(3, key);
        if (!q.row())
            return {};
        if (q.string(1) != hash)
            return Result{Status::Conflict,
                          {},
                          false,
                          "Idempotency key belongs to a different canonical request"};
        const auto id = q.string(0), receipt = q.string(2);
        const auto result = static_cast<Status>(q.integer(3));
        const auto time = now();
        Statement retry(
            db,
            "UPDATE transactions SET retryCount=retryCount+1,updatedAt=? WHERE transactionId=?");
        retry.number(1, time);
        retry.text(2, id);
        retry.done();
        Statement pending(
            db,
            "UPDATE pending_receipt SET retryCount=retryCount+1,updatedAt=? WHERE transactionId=?");
        pending.number(1, time);
        pending.text(2, id);
        pending.done();
        return Result{result, receipt, true, result == Status::Ok ? std::string{} : name(result)};
    }
    void audit(std::string_view id, const std::optional<std::string> &actor,
               std::string_view target, std::string_view op, std::string_view reason,
               std::string_view hash, Status result, const DomainRequestMetadata &metadata,
               std::int64_t time) {
        if (result != Status::Ok) {
            const auto actorKey = actor.value_or("");
            Statement budget(
                db, "INSERT INTO "
                    "denial_audit_budget(actorKey,moduleId,operation,day,totalCount,detailedCount) "
                    "VALUES(?,?,?,?,1,0) ON CONFLICT(actorKey,moduleId,operation,day) DO UPDATE "
                    "SET totalCount=totalCount+1");
            budget.text(1, actorKey);
            budget.text(2, metadata.moduleId);
            budget.text(3, op);
            budget.number(4, time / 86400000);
            budget.done();
            Statement detailed(
                db, "UPDATE denial_audit_budget SET detailedCount=detailedCount+1 WHERE actorKey=? "
                    "AND moduleId=? AND operation=? AND day=? AND detailedCount<64");
            detailed.text(1, actorKey);
            detailed.text(2, metadata.moduleId);
            detailed.text(3, op);
            detailed.number(4, time / 86400000);
            detailed.done();
            if (sqlite3_changes(db) == 0)
                return;
        }
        Statement q(
            db,
            "INSERT INTO "
            "audit(transactionId,actorUuid,operation,reason,requestHash,createdAt,moduleId,"
            "targetUuid,resultCode,requestId,permissionRevision) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        if (id.empty())
            q.null(1);
        else
            q.text(1, id);
        if (actor)
            q.text(2, *actor);
        else
            q.null(2);
        q.text(3, op);
        q.text(4, reason);
        q.text(5, hash);
        q.number(6, time);
        q.text(7, metadata.moduleId);
        q.text(8, target);
        q.number(9, static_cast<std::int64_t>(result));
        q.text(10, metadata.requestId);
        q.number(11, static_cast<std::int64_t>(metadata.permissionRevision));
        q.done();
    }
    void record(std::string_view id, std::string_view key, std::string_view hash,
                std::string_view payload, std::string_view op, const Actor &actor,
                std::string_view reason, std::int64_t time, const DomainRequestMetadata &metadata,
                std::string_view target, Status result = Status::Ok) {
        Statement q(db, "INSERT INTO "
                        "transactions(transactionId,executorScope,idempotencyKey,status,createdAt,"
                        "updatedAt,payload,requestHash,operation,actorUuid,resultCode,moduleId,"
                        "requestId,permissionRevision,retryCount) "
                        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        q.text(1, id);
        q.text(2, requestScope(metadata));
        q.text(3, key);
        q.text(4, result == Status::Ok ? "committed" : "rejected");
        q.number(5, time);
        q.number(6, time);
        q.text(7, payload);
        q.text(8, hash);
        q.text(9, op);
        q.text(10, actor.uuid());
        q.number(11, static_cast<std::int64_t>(result));
        q.text(12, metadata.moduleId);
        q.text(13, metadata.requestId);
        q.number(14, static_cast<std::int64_t>(metadata.permissionRevision));
        // Fifteen columns include the legacy retryCount, initialized explicitly.
        q.number(15, 0);
        q.done();
        audit(id, actor.uuid(), target, attributionAction(metadata, op), reason, hash, result,
              metadata, time);
    }
    void entry(std::string_view id, std::string_view target, Asset asset, std::int64_t delta,
               std::int64_t after, std::int64_t time) {
        Statement q(
            db,
            "INSERT INTO entries(transactionId,uuid,asset,delta,balanceAfter) VALUES(?,?,?,?,?)");
        q.text(1, id);
        q.text(2, target);
        q.text(3, name(asset));
        q.number(4, delta);
        q.number(5, after);
        q.done();
        event(id, target, "balance_changed",
              "{\"asset\":" + quote(name(asset)) + ",\"balance\":" + std::to_string(after) + "}",
              time);
    }
    void event(std::string_view id, std::string_view target, std::string_view type,
               std::string_view payload, std::int64_t time) {
        Statement q(db, "INSERT INTO outbox(transactionId,targetUuid,eventType,payload,createdAt) "
                        "VALUES(?,?,?,?,?)");
        q.text(1, id);
        q.text(2, target);
        q.text(3, type);
        q.text(4, payload);
        q.number(5, time);
        q.done();
        const auto eventId = sqlite3_last_insert_rowid(db);
        Statement deliveries(db, "INSERT INTO outbox_delivery(consumerId,eventId) SELECT "
                                 "consumerId,? FROM outbox_consumers");
        deliveries.number(1, eventId);
        deliveries.done();
    }
    Result complete(Transaction &tx, std::string_view id, std::string_view hash,
                    std::string receipt, std::int64_t time, Status result = Status::Ok,
                    const DomainRequestMetadata &metadata = {}) {
        if (metadata.moduleId != "core" || !metadata.requestId.empty()) {
            receipt.pop_back();
            receipt += ",\"moduleId\":" + quote(metadata.moduleId) +
                       ",\"requestId\":" + quote(metadata.requestId) + "}";
        }
        if (!metadata.canonicalRequestPayload.empty()) {
            Statement identities(db, "SELECT actorUuid,coalesce(json_extract(?,'$.targetUuid'),'') "
                                     "FROM transactions WHERE transactionId=?");
            identities.text(1, receipt);
            identities.text(2, id);
            identities.row();
            const auto actor = identities.string(0), target = identities.string(1);
            std::string accounts = "[", players = "[";
            Statement balances(db, "SELECT uuid,asset,balance,revision FROM accounts WHERE "
                                   "uuid IN (?,?) ORDER BY uuid,asset");
            balances.text(1, actor);
            balances.text(2, target);
            while (balances.row()) {
                if (accounts.size() > 1)
                    accounts += ',';
                accounts += "{\"uuid\":" + quote(balances.string(0)) +
                            ",\"asset\":" + quote(balances.string(1)) +
                            ",\"balance\":" + std::to_string(balances.integer(2)) +
                            ",\"revision\":" + std::to_string(balances.integer(3)) + "}";
            }
            Statement roles(db, "SELECT uuid,permissionRevision FROM players WHERE uuid IN (?,?) "
                                "ORDER BY uuid");
            roles.text(1, actor);
            roles.text(2, target);
            while (roles.row()) {
                if (players.size() > 1)
                    players += ',';
                players += "{\"uuid\":" + quote(roles.string(0)) +
                           ",\"permissionRevision\":" + std::to_string(roles.integer(1)) + "}";
            }
            Statement revisions(db, "SELECT coalesce((SELECT max(id) FROM entries),0),"
                                    "coalesce((SELECT max(id) FROM audit),0)");
            revisions.row();
            receipt.pop_back();
            receipt += ",\"request\":" + metadata.canonicalRequestPayload +
                       ",\"createdAt\":" + std::to_string(time) +
                       ",\"completedAt\":" + std::to_string(now()) +
                       ",\"ledgerRevision\":" + std::to_string(revisions.integer(0)) +
                       ",\"auditRevision\":" + std::to_string(revisions.integer(1)) +
                       ",\"accounts\":" + accounts + "],\"players\":" + players + "]}";
        }
        Statement q(db, "INSERT INTO receipts(transactionId,requestHash,payload) VALUES(?,?,?)");
        q.text(1, id);
        q.text(2, hash);
        q.text(3, receipt);
        q.done();
        if (result == Status::Ok) {
            Statement pending(
                db, "INSERT INTO pending_receipt(transactionId,status,payload,createdAt,updatedAt) "
                    "VALUES(?,'pending_projection',?,?,?)");
            pending.text(1, id);
            pending.text(2, receipt);
            pending.number(3, time);
            pending.number(4, time);
            pending.done();
        }
        inject(FaultPoint::BeforeCommit);
        tx.commit();
        inject(FaultPoint::AfterCommit);
        return {result, std::move(receipt), false,
                result == Status::Ok ? std::string{} : name(result)};
    }
    Result reject(Transaction &tx, const Actor &actor, std::string_view target, std::string_view op,
                  std::string_view key, std::string_view payload, std::string_view hash,
                  std::string_view reason, Status status, const DomainRequestMetadata &metadata) {
        const auto id = randomId(db);
        const auto time = now();
        record(id, key, hash, payload, op, actor, reason, time, metadata, target, status);
        return complete(tx, id, hash,
                        "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                            ",\"requestHash\":" + quote(hash) + ",\"operation\":" + quote(op) +
                            ",\"status\":\"rejected\",\"result\":" + quote(name(status)) +
                            ",\"targetUuid\":" + quote(target) + "}",
                        time, status, metadata);
    }
    Result changeAsset(const Actor &actor, std::string_view targetValue, Asset asset,
                       std::int64_t delta, std::string_view reason, std::string_view key,
                       const DomainRequestMetadata &metadata, bool ownerOnly) {
        const auto target = uuid(targetValue);
        const std::string op = ownerOnly ? "grant" : "adjust_asset";
        if (!target || !assetValid(asset) || delta == 0 || (ownerOnly && delta < 0) ||
            !reasonValid(reason) || !textValid(key, 160) || !metadataValid(metadata))
            return {Status::Invalid, {}, false, "Invalid asset request"};
        const auto payload =
            requestPayload(fields({op, executorScope, actor.uuid(), actor.xuid(), *target,
                                   name(asset), std::to_string(delta), reason}),
                           metadata);
        const auto hash = detail::sha256(payload);
        std::lock_guard lock(mutex);
        try {
            Transaction tx(db);
            if (!authenticated(db, actor))
                return {Status::PermissionDenied,
                        {},
                        false,
                        "Caller is not registered in this database"};
            if (auto old = replay(actor, key, hash, requestScope(metadata))) {
                if (old->replayed)
                    tx.commit();
                return *old;
            }
            const auto deny = [&](Status status) {
                return reject(tx, actor, *target, op, key, payload, hash, reason, status, metadata);
            };
            const bool allowed =
                ownerOnly ? owns(actor)
                          : permits(actor, asset == Asset::Coin ? Permission::MoneyAdjust
                                                                : Permission::ReputationAdjust);
            if (!allowed)
                return deny(Status::PermissionDenied);
            if (!playerExists(db, *target))
                return deny(Status::NotFound);
            const auto before = amount(db, *target, asset);
            if (accountRevision(db, *target, asset) == std::numeric_limits<std::int64_t>::max())
                return deny(Status::Overflow);
            if (delta > 0 && delta > std::numeric_limits<std::int64_t>::max() - before)
                return deny(Status::Overflow);
            if (delta < 0 && delta < -before)
                return deny(Status::InsufficientFunds);
            const auto after = before + delta, time = now();
            const auto id = randomId(db);
            record(id, key, hash, payload, op, actor, reason, time, metadata, *target);
            writeBalance(db, *target, asset, after);
            entry(id, *target, asset, delta, after, time);
            inject(FaultPoint::AfterDebit);
            return complete(
                tx, id, hash,
                "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                    ",\"requestHash\":" + quote(hash) + ",\"operation\":" + quote(op) +
                    ",\"status\":\"committed\",\"targetUuid\":" + quote(*target) +
                    ",\"asset\":" + quote(name(asset)) + ",\"amount\":" + std::to_string(delta) +
                    ",\"balance\":" + std::to_string(after) + "}",
                time, Status::Ok, metadata);
        } catch (const SqlError &e) {
            return {sqlStatus(e), {}, false, e.what()};
        }
    }
};

Core::Core(std::string p, std::string o, FaultHook f)
    : impl_(std::make_unique<Impl>(std::move(p), std::move(o), std::move(f))) {}
Core::~Core() = default;
IdentityResult Core::ensurePlayer(const VerifiedIdentity &identity) {
    const auto id = uuid(identity.uuid);
    if (!id || !validXuid(identity.xuid) ||
        (!identity.displayName.empty() && !textValid(identity.displayName, 128)))
        return {Status::Invalid, {}, false, "Invalid verified UUID/XUID"};
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement q(
            impl_->db,
            "SELECT uuid,xuid,playerId,identityVersion FROM players WHERE uuid=? OR xuid=?");
        q.text(1, *id);
        q.text(2, identity.xuid);
        if (q.row()) {
            if (q.string(0) != *id || q.string(1) != identity.xuid)
                return {Status::Conflict,
                        {},
                        false,
                        "UUID/XUID is already bound to a different identity"};
            const auto playerId = q.string(2);
            const auto version = static_cast<std::uint64_t>(q.integer(3));
            Statement seen(impl_->db,
                           "UPDATE players SET displayName=CASE WHEN ?='' THEN displayName ELSE ? "
                           "END,lastSeen=max(lastSeen,?) WHERE uuid=?");
            seen.text(1, identity.displayName);
            seen.text(2, identity.displayName);
            seen.number(3, now());
            seen.text(4, *id);
            seen.done();
            tx.commit();
            return {Status::Ok, Actor(*id, identity.xuid, playerId, version), false, {}};
        }
        auto playerId = "player_" + *id;
        playerId.erase(std::remove(playerId.begin(), playerId.end(), '-'), playerId.end());
        Statement create(impl_->db,
                         "INSERT INTO players(uuid,xuid,playerId,displayName,firstSeen,lastSeen) "
                         "VALUES(?,?,?,?,?,?)");
        create.text(1, *id);
        create.text(2, identity.xuid);
        create.text(3, playerId);
        create.text(4, identity.displayName);
        const auto time = now();
        create.number(5, time);
        create.number(6, time);
        create.done();
        Statement accounts(
            impl_->db,
            "INSERT INTO accounts(uuid,asset,balance) VALUES(?,'coin',0),(?,'reputation',0)");
        accounts.text(1, *id);
        accounts.text(2, *id);
        accounts.done();
        tx.commit();
        return {Status::Ok, Actor(*id, identity.xuid, playerId, 1), true, {}};
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
PlayerResult Core::player(std::string_view value) const {
    const auto canonical = uuid(value);
    if (!canonical && !textValid(value, 80))
        return {Status::Invalid, {}, "Invalid player identifier"};
    std::lock_guard lock(impl_->mutex);
    try {
        Statement q(impl_->db, "SELECT "
                               "playerId,uuid,xuid,displayName,firstSeen,lastSeen,identityVersion,"
                               "permissionRevision FROM players WHERE uuid=? OR playerId=?");
        q.text(1, canonical.value_or(""));
        q.text(2, value);
        if (!q.row())
            return {Status::NotFound, {}, "Player is not registered"};
        return {Status::Ok,
                PlayerSnapshot{q.string(0), q.string(1), q.string(2), q.string(3), q.integer(4),
                               q.integer(5), static_cast<std::uint64_t>(q.integer(6)),
                               static_cast<std::uint64_t>(q.integer(7))},
                {}};
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, e.what()};
    }
}
IdentityResult Core::restoreActor(std::string_view value) const {
    const auto found = player(value);
    if (found.status != Status::Ok)
        return {found.status, {}, false, found.error};
    const auto &p = *found.player;
    return {Status::Ok, Actor(p.uuid, p.xuid, p.playerId, p.identityVersion), false, {}};
}
std::optional<std::uint64_t> Core::permissionRevision(std::string_view value) const {
    const auto p = player(value);
    return p.player ? std::optional<std::uint64_t>{p.player->permissionRevision} : std::nullopt;
}
PlayerResult Core::findPlayerByDisplayName(std::string_view displayName) const {
    if (!textValid(displayName, 128))
        return {Status::Invalid, {}, "Invalid display name"};
    std::string playerId;
    {
        std::lock_guard lock(impl_->mutex);
        try {
            Statement q(impl_->db, "SELECT playerId FROM players WHERE displayName=? LIMIT 2");
            q.text(1, displayName);
            if (!q.row())
                return {Status::NotFound, {}, "Player not found"};
            playerId = q.string(0);
            if (q.row())
                return {Status::Conflict, {}, "Display name is ambiguous"};
        } catch (const SqlError &e) {
            return {sqlStatus(e), {}, e.what()};
        }
    }
    return player(playerId);
}
DiagnosticResult Core::selfcheck() const {
    std::lock_guard lock(impl_->mutex);
    try {
        // A read transaction gives one coherent snapshot while other connections write.
        execute(impl_->db, "BEGIN");
        struct ReadEnd {
            sqlite3 *db;
            ~ReadEnd() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
        } end{impl_->db};
        verifyDatabase(impl_->db);
        Statement counts(
            impl_->db,
            "SELECT (SELECT count(*) FROM players),(SELECT count(*) FROM roles),(SELECT count(*) "
            "FROM transactions),(SELECT count(*) FROM entries),(SELECT count(*) FROM "
            "audit),(SELECT count(*) FROM receipts),(SELECT count(*) FROM outbox),(SELECT count(*) "
            "FROM outbox_delivery WHERE status!='acknowledged')");
        counts.row();
        Diagnostics value{counts.integer(0),
                          counts.integer(1),
                          counts.integer(2),
                          counts.integer(3),
                          counts.integer(4),
                          counts.integer(5),
                          counts.integer(6),
                          counts.integer(7),
                          true,
                          true};
        std::map<std::pair<std::string, std::string>, std::int64_t> balances;
        Statement entries(impl_->db,
                          "SELECT uuid,asset,delta,balanceAfter FROM entries ORDER BY id");
        while (entries.row()) {
            auto &before = balances[{entries.string(0), entries.string(1)}];
            const auto delta = entries.integer(2);
            if ((delta > 0 && delta > std::numeric_limits<std::int64_t>::max() - before) ||
                (delta < 0 && delta < -before)) {
                value.ledgerConsistent = false;
                break;
            }
            before += delta;
            if (before != entries.integer(3))
                value.ledgerConsistent = false;
        }
        Statement accounts(impl_->db, "SELECT uuid,asset,balance FROM accounts");
        while (accounts.row())
            if (balances[{accounts.string(0), accounts.string(1)}] != accounts.integer(2))
                value.ledgerConsistent = false;
        Statement receipts(
            impl_->db,
            "SELECT 1 FROM transactions t LEFT JOIN receipts r ON r.transactionId=t.transactionId "
            "WHERE r.transactionId IS NULL OR r.requestHash!=t.requestHash UNION ALL SELECT 1 FROM "
            "entries e JOIN transactions t ON t.transactionId=e.transactionId WHERE "
            "t.status!='committed' LIMIT 1");
        value.receiptsConsistent = !receipts.row();
        return {Status::Ok, value, {}};
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, e.what()};
    }
}
BalanceResult Core::balance(std::string_view value, Asset asset) const {
    const auto id = uuid(value);
    if (!id || !assetValid(asset))
        return {Status::Invalid, 0, "Invalid account"};
    std::lock_guard lock(impl_->mutex);
    try {
        if (!playerExists(impl_->db, *id))
            return {Status::NotFound, 0, "Player is not registered"};
        Statement q(impl_->db, "SELECT balance,revision,(SELECT coalesce(max(id),0) FROM entries) "
                               "FROM accounts WHERE uuid=? AND asset=?");
        q.text(1, *id);
        q.text(2, name(asset));
        if (!q.row())
            return {Status::StorageError, 0, "Account missing"};
        return {Status::Ok,
                q.integer(0),
                {},
                static_cast<std::uint64_t>(q.integer(1)),
                static_cast<std::uint64_t>(q.integer(2))};
    } catch (const SqlError &e) {
        return {sqlStatus(e), 0, e.what()};
    }
}
bool Core::hasRole(const Actor &actor, Role role) const {
    if (!roleValid(role))
        return false;
    std::lock_guard lock(impl_->mutex);
    try {
        return impl_->role(actor, role);
    } catch (const SqlError &) {
        return false;
    }
}
bool Core::hasPermission(const Actor &actor, Permission permission) const {
    std::lock_guard lock(impl_->mutex);
    try {
        return impl_->permits(actor, permission);
    } catch (const SqlError &) {
        return false;
    }
}
Result Core::ownerGrant(const Actor &actor, std::string_view target, Asset asset,
                        std::int64_t value, std::string_view reason, std::string_view key,
                        const DomainRequestMetadata &metadata) {
    return impl_->changeAsset(actor, target, asset, value, reason, key, metadata, true);
}
Result Core::adjustAsset(const Actor &actor, std::string_view target, Asset asset,
                         std::int64_t delta, std::string_view reason, std::string_view key,
                         const DomainRequestMetadata &metadata) {
    return impl_->changeAsset(actor, target, asset, delta, reason, key, metadata, false);
}
Result Core::transfer(const Actor &actor, std::string_view targetValue, std::int64_t value,
                      std::string_view reason, std::string_view key,
                      const DomainRequestMetadata &metadata) {
    const auto target = uuid(targetValue);
    if (!target || value <= 0 || *target == actor.uuid() || !reasonValid(reason) ||
        !textValid(key, 160) || !metadataValid(metadata))
        return {Status::Invalid, {}, false, "Invalid transfer request"};
    const auto payload =
        requestPayload(fields({"transfer", executorScope, actor.uuid(), actor.xuid(), *target,
                               "coin", std::to_string(value), reason}),
                       metadata);
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!authenticated(impl_->db, actor))
            return {Status::PermissionDenied,
                    {},
                    false,
                    "Caller identity is not bound to this database"};
        if (auto old = impl_->replay(actor, key, hash, requestScope(metadata))) {
            if (old->replayed)
                tx.commit();
            return *old;
        }
        const auto deny = [&](Status status) {
            return impl_->reject(tx, actor, *target, "transfer", key, payload, hash, reason, status,
                                 metadata);
        };
        if (!impl_->permits(actor, Permission::MoneyTransfer))
            return deny(Status::PermissionDenied);
        if (!playerExists(impl_->db, *target))
            return deny(Status::NotFound);
        const auto before = amount(impl_->db, actor.uuid(), Asset::Coin),
                   targetBefore = amount(impl_->db, *target, Asset::Coin);
        if (accountRevision(impl_->db, actor.uuid(), Asset::Coin) ==
                std::numeric_limits<std::int64_t>::max() ||
            accountRevision(impl_->db, *target, Asset::Coin) ==
                std::numeric_limits<std::int64_t>::max())
            return deny(Status::Overflow);
        if (before < value)
            return deny(Status::InsufficientFunds);
        if (value > std::numeric_limits<std::int64_t>::max() - targetBefore)
            return deny(Status::Overflow);
        const auto after = before - value, targetAfter = targetBefore + value, time = now();
        const auto id = randomId(impl_->db);
        impl_->record(id, key, hash, payload, "transfer", actor, reason, time, metadata, *target);
        writeBalance(impl_->db, actor.uuid(), Asset::Coin, after);
        impl_->entry(id, actor.uuid(), Asset::Coin, -value, after, time);
        impl_->inject(FaultPoint::AfterDebit);
        writeBalance(impl_->db, *target, Asset::Coin, targetAfter);
        impl_->entry(id, *target, Asset::Coin, value, targetAfter, time);
        return impl_->complete(
            tx, id, hash,
            "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                ",\"requestHash\":" + quote(hash) +
                ",\"operation\":\"transfer\",\"status\":\"committed\",\"sourceUuid\":" +
                quote(actor.uuid()) + ",\"targetUuid\":" + quote(*target) + ",\"amount\":" +
                std::to_string(value) + ",\"sourceBalance\":" + std::to_string(after) +
                ",\"targetBalance\":" + std::to_string(targetAfter) + "}",
            time, Status::Ok, metadata);
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
Result Core::setRole(const Actor &actor, std::string_view targetValue, Role role, bool enabled,
                     std::string_view reason, std::string_view key,
                     const DomainRequestMetadata &metadata) {
    const auto target = uuid(targetValue);
    if (!target || !roleValid(role) || !reasonValid(reason) || !textValid(key, 160) ||
        !metadataValid(metadata))
        return {Status::Invalid, {}, false, "Invalid role request"};
    const auto payload =
        requestPayload(fields({"set_role", executorScope, actor.uuid(), actor.xuid(), *target,
                               name(role), enabled ? "true" : "false", reason}),
                       metadata);
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!authenticated(impl_->db, actor))
            return {
                Status::PermissionDenied, {}, false, "Caller is not registered in this database"};
        if (auto old = impl_->replay(actor, key, hash, requestScope(metadata))) {
            if (old->replayed)
                tx.commit();
            return *old;
        }
        const auto deny = [&](Status status) {
            return impl_->reject(tx, actor, *target, "set_role", key, payload, hash, reason, status,
                                 metadata);
        };
        if (!impl_->owns(actor) || role == Role::Owner || role == Role::Player)
            return deny(Status::PermissionDenied);
        if (!playerExists(impl_->db, *target))
            return deny(Status::NotFound);
        Statement revision(impl_->db, "SELECT permissionRevision FROM players WHERE uuid=?");
        revision.text(1, *target);
        revision.row();
        if (revision.integer(0) == std::numeric_limits<std::int64_t>::max())
            return deny(Status::Overflow);
        const auto id = randomId(impl_->db);
        const auto time = now();
        impl_->record(id, key, hash, payload, "set_role", actor, reason, time, metadata, *target);
        Statement q(impl_->db,
                    enabled ? "INSERT INTO roles(uuid,role) VALUES(?,?) ON CONFLICT DO NOTHING"
                            : "DELETE FROM roles WHERE uuid=? AND role=?");
        q.text(1, *target);
        q.text(2, name(role));
        q.done();
        Statement version(
            impl_->db, "UPDATE players SET permissionRevision=permissionRevision+1 WHERE uuid=?");
        version.text(1, *target);
        version.done();
        impl_->event(id, *target, "role_changed",
                     "{\"role\":" + quote(name(role)) +
                         ",\"enabled\":" + (enabled ? "true" : "false") +
                         ",\"permissionRevision\":" + std::to_string(revision.integer(0) + 1) + "}",
                     time);
        return impl_->complete(
            tx, id, hash,
            "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                ",\"requestHash\":" + quote(hash) +
                ",\"operation\":\"set_role\",\"status\":\"committed\",\"targetUuid\":" +
                quote(*target) + ",\"role\":" + quote(name(role)) +
                ",\"enabled\":" + (enabled ? "true" : "false") +
                ",\"permissionRevision\":" + std::to_string(revision.integer(0) + 1) + "}",
            time, Status::Ok, metadata);
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
Result Core::receipt(const Actor &actor, std::string_view key,
                     const DomainRequestMetadata &metadata) const {
    if (!textValid(key, 160) || !metadataValid(metadata))
        return {Status::Invalid, {}, false, "Invalid receipt key"};
    std::lock_guard lock(impl_->mutex);
    try {
        if (!authenticated(impl_->db, actor))
            return {Status::PermissionDenied, {}, false, "Caller is not registered"};
        Statement q(impl_->db, "SELECT t.resultCode,r.payload FROM transactions t JOIN receipts r "
                               "ON r.transactionId=t.transactionId WHERE t.executorScope=? AND "
                               "t.actorUuid=? AND t.idempotencyKey=?");
        q.text(1, requestScope(metadata));
        q.text(2, actor.uuid());
        q.text(3, key);
        if (!q.row())
            return {Status::NotFound, {}, false, "Receipt not found"};
        const auto result = static_cast<Status>(q.integer(0));
        return {result, q.string(1), true, result == Status::Ok ? std::string{} : name(result)};
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
Result Core::receiptById(const Actor &actor, std::string_view transactionId,
                         const DomainRequestMetadata &metadata) const {
    if (!textValid(transactionId, 80) || !metadataValid(metadata))
        return {Status::Invalid, {}, false, "Invalid transaction identifier"};
    std::lock_guard lock(impl_->mutex);
    try {
        if (!authenticated(impl_->db, actor))
            return {Status::PermissionDenied, {}, false, "Caller is not registered"};
        Statement q(impl_->db, "SELECT t.resultCode,r.payload FROM transactions t JOIN receipts r "
                               "ON r.transactionId=t.transactionId WHERE t.executorScope=? AND "
                               "t.actorUuid=? AND t.transactionId=?");
        q.text(1, requestScope(metadata));
        q.text(2, actor.uuid());
        q.text(3, transactionId);
        if (!q.row())
            return {Status::NotFound, {}, false, "Receipt not found"};
        const auto result = static_cast<Status>(q.integer(0));
        return {result, q.string(1), true, result == Status::Ok ? std::string{} : name(result)};
    } catch (const SqlError &error) {
        return {sqlStatus(error), {}, false, error.what()};
    }
}
Status Core::auditDenied(const DenialRecord &value) {
    const DomainRequestMetadata metadata{value.moduleId, value.action, value.requestId,
                                         value.permissionRevision};
    if (!metadataValid(metadata) || value.action.empty() || !reasonValid(value.reason) ||
        value.result == Status::Ok || value.result < Status::Busy ||
        value.result > Status::StorageError ||
        (!value.targetUuid.empty() && !textValid(value.targetUuid, 80)) ||
        (!value.transactionId.empty() && !textValid(value.transactionId, 80)))
        return Status::Invalid;
    auto actor = value.actorUuid;
    if (actor) {
        const auto canonical = uuid(*actor);
        if (!canonical)
            return Status::Invalid;
        actor = *canonical;
    }
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (actor && !playerExists(impl_->db, *actor))
            actor.reset();
        if (!value.transactionId.empty()) {
            Statement find(impl_->db, "SELECT 1 FROM transactions WHERE transactionId=?");
            find.text(1, value.transactionId);
            if (!find.row())
                return Status::NotFound;
        }
        impl_->audit(value.transactionId, actor, value.targetUuid, value.action, value.reason, {},
                     value.result, metadata, now());
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}
Result Core::recordRejectedRequest(const Actor &actor, std::string_view targetValue,
                                   std::string_view operation, std::string_view payload,
                                   std::string_view reason, std::string_view key, Status result,
                                   const DomainRequestMetadata &metadata) {
    if ((!targetValue.empty() && !textValid(targetValue, 80)) || !textValid(operation, 80) ||
        !textValid(payload, 16384) || !reasonValid(reason) || !textValid(key, 160) ||
        !metadataValid(metadata) || result <= Status::Ok || result > Status::StorageError ||
        (!metadata.canonicalRequestPayload.empty() && metadata.canonicalRequestPayload != payload))
        return {Status::Invalid, {}, false, "Invalid runtime rejection request"};
    const auto target = uuid(targetValue).value_or(std::string(targetValue));
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!authenticated(impl_->db, actor))
            return {Status::PermissionDenied, {}, false, "Caller is not registered"};
        Statement find(impl_->db,
                       "SELECT t.transactionId,t.requestHash,t.resultCode,r.payload FROM "
                       "transactions t JOIN receipts r ON r.transactionId=t.transactionId WHERE "
                       "t.executorScope=? AND t.actorUuid=? AND t.idempotencyKey=?");
        find.text(1, requestScope(metadata));
        find.text(2, actor.uuid());
        find.text(3, key);
        if (find.row() && find.string(1) != hash) {
            const auto id = find.string(0), originalReceipt = find.string(3);
            impl_->audit(id, actor.uuid(), target, attributionAction(metadata, operation), reason,
                         hash, Status::Conflict, metadata, now());
            tx.commit();
            return {Status::Conflict, originalReceipt, false,
                    "Idempotency key belongs to a different canonical request"};
        }
        if (auto previous = impl_->replay(actor, key, hash, requestScope(metadata))) {
            if (previous->replayed)
                tx.commit();
            return *previous;
        }
        return impl_->reject(tx, actor, target, operation, key, payload, hash, reason, result,
                             metadata);
    } catch (const SqlError &error) {
        return {sqlStatus(error), {}, false, error.what()};
    }
}
Status Core::registerConsumer(std::string_view consumerId) {
    if (!textValid(consumerId, 80))
        return Status::Invalid;
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement consumer(impl_->db, "INSERT INTO outbox_consumers(consumerId,createdAt) "
                                      "VALUES(?,?) ON CONFLICT DO NOTHING");
        consumer.text(1, consumerId);
        consumer.number(2, now());
        consumer.done();
        Statement backfill(impl_->db, "INSERT INTO outbox_delivery(consumerId,eventId) SELECT ?,id "
                                      "FROM outbox WHERE true ON CONFLICT DO NOTHING");
        backfill.text(1, consumerId);
        backfill.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}
std::vector<OutboxEvent> Core::outboxFor(std::string_view consumerId, std::size_t limit) const {
    if (!textValid(consumerId, 80) || limit == 0 || limit > 1000)
        throw std::invalid_argument("Invalid outbox consumer or limit");
    std::lock_guard lock(impl_->mutex);
    Statement q(
        impl_->db,
        "SELECT "
        "o.id,o.transactionId,o.targetUuid,o.eventType,o.payload,d.retryCount,d.nextAttemptAt,d."
        "lastError,d.status FROM outbox o JOIN outbox_delivery d ON d.eventId=o.id WHERE "
        "d.consumerId=? AND d.status!='acknowledged' AND d.nextAttemptAt<=? ORDER BY o.id LIMIT ?");
    q.text(1, consumerId);
    q.number(2, now());
    q.number(3, static_cast<std::int64_t>(limit));
    std::vector<OutboxEvent> events;
    while (q.row())
        events.push_back({q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                          static_cast<std::uint64_t>(q.integer(5)), q.integer(6), q.string(7),
                          q.string(8)});
    return events;
}
Status Core::recordOutboxAttempt(std::string_view consumerId, std::int64_t eventId,
                                 std::string_view error, std::int64_t delay, bool offline) {
    if (!textValid(consumerId, 80) || eventId <= 0 || !textValid(error, 512) || delay < 0 ||
        delay > 86400000)
        return Status::Invalid;
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement find(
            impl_->db,
            "SELECT status,retryCount FROM outbox_delivery WHERE consumerId=? AND eventId=?");
        find.text(1, consumerId);
        find.number(2, eventId);
        if (!find.row())
            return Status::NotFound;
        if (find.string(0) == "acknowledged") {
            tx.commit();
            return Status::Ok;
        }
        if (find.integer(1) == std::numeric_limits<std::int64_t>::max())
            return Status::Overflow;
        Statement retry(impl_->db, "UPDATE outbox_delivery SET "
                                   "status=?,retryCount=retryCount+1,nextAttemptAt=?,lastError=? "
                                   "WHERE consumerId=? AND eventId=?");
        retry.text(1, offline ? "offline" : "retry");
        retry.number(2, now() + delay);
        retry.text(3, error);
        retry.text(4, consumerId);
        retry.number(5, eventId);
        retry.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}
Status Core::acknowledgeOutbox(std::string_view consumerId, std::int64_t eventId) {
    if (!textValid(consumerId, 80) || eventId <= 0)
        return Status::Invalid;
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement find(impl_->db, "SELECT o.transactionId FROM outbox_delivery d JOIN outbox o ON "
                                  "o.id=d.eventId WHERE d.consumerId=? AND d.eventId=?");
        find.text(1, consumerId);
        find.number(2, eventId);
        if (!find.row())
            return Status::NotFound;
        const auto id = find.string(0);
        Statement ack(impl_->db, "UPDATE outbox_delivery SET "
                                 "status='acknowledged',acknowledgedAt=coalesce(acknowledgedAt,?) "
                                 "WHERE consumerId=? AND eventId=?");
        ack.number(1, now());
        ack.text(2, consumerId);
        ack.number(3, eventId);
        ack.done();
        Statement aggregate(
            impl_->db, "UPDATE outbox SET acknowledged=1,acknowledgedAt=coalesce(acknowledgedAt,?) "
                       "WHERE id=? AND NOT EXISTS(SELECT 1 FROM outbox_delivery WHERE eventId=? "
                       "AND status!='acknowledged')");
        aggregate.number(1, now());
        aggregate.number(2, eventId);
        aggregate.number(3, eventId);
        aggregate.done();
        Statement pending(impl_->db,
                          "DELETE FROM pending_receipt WHERE transactionId=? AND NOT EXISTS(SELECT "
                          "1 FROM outbox_delivery d JOIN outbox o ON o.id=d.eventId WHERE "
                          "o.transactionId=? AND d.status!='acknowledged') AND NOT EXISTS(SELECT 1 "
                          "FROM outbox WHERE transactionId=? AND acknowledged=0)");
        pending.text(1, id);
        pending.text(2, id);
        pending.text(3, id);
        pending.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}
std::vector<OutboxEvent> Core::pendingOutbox(std::size_t limit) const {
    if (limit == 0 || limit > 1000)
        throw std::invalid_argument("Outbox limit must be 1..1000");
    std::lock_guard lock(impl_->mutex);
    Statement q(impl_->db, "SELECT id,transactionId,targetUuid,eventType,payload FROM outbox WHERE "
                           "acknowledged=0 ORDER BY id LIMIT ?");
    q.number(1, static_cast<std::int64_t>(limit));
    std::vector<OutboxEvent> result;
    while (q.row())
        result.push_back(
            {q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4), 0, 0, {}, {}});
    return result;
}
std::vector<OutboxEvent> Core::eventsAfter(std::int64_t afterEventId, std::size_t limit) const {
    if (afterEventId < 0 || limit == 0 || limit > 1000)
        throw std::invalid_argument("Invalid outbox history cursor or limit");
    std::lock_guard lock(impl_->mutex);
    Statement q(impl_->db, "SELECT o.id,o.transactionId,o.targetUuid,o.eventType,o.payload,"
                           "o.acknowledged,o.createdAt,t.requestId FROM outbox o JOIN transactions "
                           "t ON t.transactionId=o.transactionId WHERE o.id>? ORDER BY o.id LIMIT ?");
    q.number(1, afterEventId);
    q.number(2, static_cast<std::int64_t>(limit));
    std::vector<OutboxEvent> events;
    while (q.row())
        events.push_back({q.integer(0),
                          q.string(1),
                          q.string(2),
                          q.string(3),
                          q.string(4),
                          0,
                          0,
                          {},
                          q.integer(5) ? "acknowledged" : "pending",
                          q.integer(6),
                          q.string(7)});
    return events;
}
Status Core::acknowledgeOutbox(std::int64_t id) {
    if (id <= 0)
        return Status::Invalid;
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement find(impl_->db, "SELECT transactionId FROM outbox WHERE id=?");
        find.number(1, id);
        if (!find.row())
            return Status::NotFound;
        const auto transaction = find.string(0);
        Statement q(impl_->db,
                    "UPDATE outbox SET acknowledged=1,acknowledgedAt=coalesce(acknowledgedAt,?) "
                    "WHERE id=?");
        q.number(1, now());
        q.number(2, id);
        q.done();
        Statement pending(impl_->db,
                          "DELETE FROM pending_receipt WHERE transactionId=? AND NOT EXISTS(SELECT "
                          "1 FROM outbox WHERE transactionId=? AND acknowledged=0) AND NOT "
                          "EXISTS(SELECT 1 FROM outbox_delivery d JOIN outbox o ON o.id=d.eventId "
                          "WHERE o.transactionId=? AND d.status!='acknowledged')");
        pending.text(1, transaction);
        pending.text(2, transaction);
        pending.text(3, transaction);
        pending.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}
Status Core::backup(std::string_view destination) const {
    std::lock_guard lock(impl_->mutex);
    try {
        impl_->onlineBackup(std::string(destination));
        return Status::Ok;
    } catch (const std::invalid_argument &) {
        return Status::Invalid;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    } catch (const std::runtime_error &) {
        return Status::StorageError;
    }
}
} // namespace eternal::core
