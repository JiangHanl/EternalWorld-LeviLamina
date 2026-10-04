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
        check(db,
              sqlite3_bind_text(s, index, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT));
    }
    void number(int index, std::int64_t v) { check(db, sqlite3_bind_int64(s, index, v)); }
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
void writeBalance(sqlite3 *db, std::string_view id, Asset asset, std::int64_t value) {
    Statement q(db, "UPDATE accounts SET balance=? WHERE uuid=? AND asset=?");
    q.number(1, value);
    q.text(2, id);
    q.text(3, name(asset));
    q.done();
    if (sqlite3_changes(db) != 1)
        throw SqlError(SQLITE_CORRUPT, "Account update failed");
}
bool authenticated(sqlite3 *db, const Actor &actor) {
    Statement q(db, "SELECT 1 FROM players WHERE uuid=? AND xuid=?");
    q.text(1, actor.uuid());
    q.text(2, actor.xuid());
    return q.row();
}
bool assetValid(Asset a) { return a == Asset::Coin || a == Asset::Reputation; }
bool roleValid(Role r) { return r >= Role::Build && r <= Role::Operations; }
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
    default:
        return "invalid";
    }
}
Actor::Actor(std::string u, std::string x) : uuid_(std::move(u)), xuid_(std::move(x)) {}

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
            }
            {
                Statement q(db, "PRAGMA journal_mode=WAL");
                if (!q.row() || q.string(0) != "wal")
                    throw SqlError(SQLITE_ERROR, "WAL unavailable");
            }
            execute(db, "PRAGMA synchronous=FULL");
            if (version == 0) {
                if (existed)
                    onlineBackup(path + ".pre-migration-001-" + std::to_string(now()) + ".sqlite");
                Transaction tx(db);
                execute(db, std::string(detail::schemaV1).c_str());
                Statement meta(db, "INSERT INTO metadata(key,value) VALUES('ownerXuid',?)");
                meta.text(1, owner);
                meta.done();
                Statement migration(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) VALUES(1,?,?)");
                migration.text(1, detail::sha256(detail::schemaV1));
                migration.number(2, now());
                migration.done();
                verifyDatabase(db);
                tx.commit();
            }
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
    void inject(FaultPoint point) {
        if (fault)
            fault(point);
    }
    std::optional<Result> replay(const Actor &actor, std::string_view key, std::string_view hash) {
        Statement q(db, "SELECT t.transactionId,t.requestHash,r.payload FROM transactions AS t "
                        "JOIN receipts AS r ON r.transactionId=t.transactionId "
                        "WHERE t.executorScope=? AND t.actorUuid=? AND t.idempotencyKey=?");
        q.text(1, executorScope);
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
        return Result{Status::Ok, receipt, true, {}};
    }
    void record(std::string_view id, std::string_view key, std::string_view hash,
                std::string_view payload, std::string_view op, const Actor &actor,
                std::string_view reason, std::int64_t time) {
        Statement q(db, "INSERT INTO "
                        "transactions(transactionId,executorScope,idempotencyKey,status,createdAt,"
                        "updatedAt,payload,requestHash,operation,actorUuid) "
                        "VALUES(?,?,?,'committed',?,?,?,?,?,?)");
        q.text(1, id);
        q.text(2, executorScope);
        q.text(3, key);
        q.number(4, time);
        q.number(5, time);
        q.text(6, payload);
        q.text(7, hash);
        q.text(8, op);
        q.text(9, actor.uuid());
        q.done();
        Statement audit(
            db, "INSERT INTO audit(transactionId,actorUuid,operation,reason,requestHash,createdAt) "
                "VALUES(?,?,?,?,?,?)");
        audit.text(1, id);
        audit.text(2, actor.uuid());
        audit.text(3, op);
        audit.text(4, reason);
        audit.text(5, hash);
        audit.number(6, time);
        audit.done();
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
    }
    Result complete(Transaction &tx, std::string_view id, std::string_view hash,
                    std::string receipt, std::int64_t time) {
        Statement q(db, "INSERT INTO receipts(transactionId,requestHash,payload) VALUES(?,?,?)");
        q.text(1, id);
        q.text(2, hash);
        q.text(3, receipt);
        q.done();
        Statement pending(
            db, "INSERT INTO pending_receipt(transactionId,status,payload,createdAt,updatedAt) "
                "VALUES(?,'pending_projection',?,?,?)");
        pending.text(1, id);
        pending.text(2, receipt);
        pending.number(3, time);
        pending.number(4, time);
        pending.done();
        inject(FaultPoint::BeforeCommit);
        tx.commit();
        inject(FaultPoint::AfterCommit);
        return {Status::Ok, std::move(receipt), false, {}};
    }
};

Core::Core(std::string p, std::string o, FaultHook f)
    : impl_(std::make_unique<Impl>(std::move(p), std::move(o), std::move(f))) {}
Core::~Core() = default;
IdentityResult Core::ensurePlayer(const VerifiedIdentity &identity) {
    const auto id = uuid(identity.uuid);
    if (!id || !validXuid(identity.xuid))
        return {Status::Invalid, {}, false, "Invalid verified UUID/XUID"};
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        Statement q(impl_->db, "SELECT uuid,xuid FROM players WHERE uuid=? OR xuid=?");
        q.text(1, *id);
        q.text(2, identity.xuid);
        if (q.row()) {
            if (q.string(0) != *id || q.string(1) != identity.xuid)
                return {Status::Conflict,
                        {},
                        false,
                        "UUID/XUID is already bound to a different identity"};
            tx.commit();
            return {Status::Ok, Actor(*id, identity.xuid), false, {}};
        }
        Statement create(impl_->db, "INSERT INTO players(uuid,xuid) VALUES(?,?)");
        create.text(1, *id);
        create.text(2, identity.xuid);
        create.done();
        Statement accounts(
            impl_->db,
            "INSERT INTO accounts(uuid,asset,balance) VALUES(?,'coin',0),(?,'reputation',0)");
        accounts.text(1, *id);
        accounts.text(2, *id);
        accounts.done();
        tx.commit();
        return {Status::Ok, Actor(*id, identity.xuid), true, {}};
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
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
        return {Status::Ok, amount(impl_->db, *id, asset), {}};
    } catch (const SqlError &e) {
        return {sqlStatus(e), 0, e.what()};
    }
}
bool Core::hasRole(const Actor &actor, Role role) const {
    if (!roleValid(role))
        return false;
    std::lock_guard lock(impl_->mutex);
    try {
        if (!authenticated(impl_->db, actor))
            return false;
        if (actor.xuid() == impl_->owner)
            return true;
        Statement q(impl_->db, "SELECT 1 FROM roles WHERE uuid=? AND role=?");
        q.text(1, actor.uuid());
        q.text(2, name(role));
        return q.row();
    } catch (const SqlError &) {
        return false;
    }
}
Result Core::ownerGrant(const Actor &actor, std::string_view targetValue, Asset asset,
                        std::int64_t value, std::string_view reason, std::string_view key) {
    const auto target = uuid(targetValue);
    if (!target || !assetValid(asset) || value <= 0 || !reasonValid(reason) || !textValid(key, 160))
        return {Status::Invalid, {}, false, "Invalid grant request"};
    const auto payload = fields({"grant", executorScope, actor.uuid(), actor.xuid(), *target,
                                 name(asset), std::to_string(value), reason});
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!impl_->owns(actor))
            return {
                Status::PermissionDenied, {}, false, "Only the verified owner may grant assets"};
        if (auto old = impl_->replay(actor, key, hash)) {
            if (old->status == Status::Ok)
                tx.commit();
            return *old;
        }
        if (!playerExists(impl_->db, *target))
            return {Status::NotFound, {}, false, "Target is not registered"};
        const auto before = amount(impl_->db, *target, asset);
        if (value > std::numeric_limits<std::int64_t>::max() - before)
            return {Status::Overflow, {}, false, "Account overflow"};
        const auto after = before + value, time = now();
        const auto id = randomId(impl_->db);
        impl_->record(id, key, hash, payload, "grant", actor, reason, time);
        writeBalance(impl_->db, *target, asset, after);
        impl_->entry(id, *target, asset, value, after, time);
        return impl_->complete(
            tx, id, hash,
            "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                ",\"requestHash\":" + quote(hash) +
                ",\"operation\":\"grant\",\"status\":\"committed\",\"targetUuid\":" +
                quote(*target) + ",\"asset\":" + quote(name(asset)) + ",\"amount\":" +
                std::to_string(value) + ",\"balance\":" + std::to_string(after) + "}",
            time);
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
Result Core::transfer(const Actor &actor, std::string_view targetValue, std::int64_t value,
                      std::string_view reason, std::string_view key) {
    const auto target = uuid(targetValue);
    if (!target || value <= 0 || *target == actor.uuid() || !reasonValid(reason) ||
        !textValid(key, 160))
        return {Status::Invalid, {}, false, "Invalid transfer request"};
    const auto payload = fields({"transfer", executorScope, actor.uuid(), actor.xuid(), *target,
                                 "coin", std::to_string(value), reason});
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!authenticated(impl_->db, actor))
            return {Status::PermissionDenied,
                    {},
                    false,
                    "Caller identity is not bound to this database"};
        if (auto old = impl_->replay(actor, key, hash)) {
            if (old->status == Status::Ok)
                tx.commit();
            return *old;
        }
        if (!playerExists(impl_->db, *target))
            return {Status::NotFound, {}, false, "Target is not registered"};
        const auto before = amount(impl_->db, actor.uuid(), Asset::Coin),
                   targetBefore = amount(impl_->db, *target, Asset::Coin);
        if (before < value)
            return {Status::InsufficientFunds, {}, false, "Insufficient funds"};
        if (value > std::numeric_limits<std::int64_t>::max() - targetBefore)
            return {Status::Overflow, {}, false, "Recipient overflow"};
        const auto after = before - value, targetAfter = targetBefore + value, time = now();
        const auto id = randomId(impl_->db);
        impl_->record(id, key, hash, payload, "transfer", actor, reason, time);
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
            time);
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
    }
}
Result Core::setRole(const Actor &actor, std::string_view targetValue, Role role, bool enabled,
                     std::string_view reason, std::string_view key) {
    const auto target = uuid(targetValue);
    if (!target || !roleValid(role) || !reasonValid(reason) || !textValid(key, 160))
        return {Status::Invalid, {}, false, "Invalid role request"};
    const auto payload = fields({"set_role", executorScope, actor.uuid(), actor.xuid(), *target,
                                 name(role), enabled ? "true" : "false", reason});
    const auto hash = detail::sha256(payload);
    std::lock_guard lock(impl_->mutex);
    try {
        Transaction tx(impl_->db);
        if (!impl_->owns(actor))
            return {
                Status::PermissionDenied, {}, false, "Only the verified owner may assign roles"};
        if (auto old = impl_->replay(actor, key, hash)) {
            if (old->status == Status::Ok)
                tx.commit();
            return *old;
        }
        if (!playerExists(impl_->db, *target))
            return {Status::NotFound, {}, false, "Target is not registered"};
        const auto id = randomId(impl_->db);
        const auto time = now();
        impl_->record(id, key, hash, payload, "set_role", actor, reason, time);
        Statement q(impl_->db,
                    enabled ? "INSERT INTO roles(uuid,role) VALUES(?,?) ON CONFLICT DO NOTHING"
                            : "DELETE FROM roles WHERE uuid=? AND role=?");
        q.text(1, *target);
        q.text(2, name(role));
        q.done();
        impl_->event(id, *target, "role_changed",
                     "{\"role\":" + quote(name(role)) +
                         ",\"enabled\":" + (enabled ? "true" : "false") + "}",
                     time);
        return impl_->complete(
            tx, id, hash,
            "{\"transactionId\":" + quote(id) + ",\"idempotencyKey\":" + quote(key) +
                ",\"requestHash\":" + quote(hash) +
                ",\"operation\":\"set_role\",\"status\":\"committed\",\"targetUuid\":" +
                quote(*target) + ",\"role\":" + quote(name(role)) +
                ",\"enabled\":" + (enabled ? "true" : "false") + "}",
            time);
    } catch (const SqlError &e) {
        return {sqlStatus(e), {}, false, e.what()};
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
        result.push_back({q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4)});
    return result;
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
                          "1 FROM outbox WHERE transactionId=? AND acknowledged=0)");
        pending.text(1, transaction);
        pending.text(2, transaction);
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
