#include "Commerce.hpp"
#include "Schema.hpp"
#include "Sha256.hpp"
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>

namespace eternal::commerce {
namespace {
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
           : base == SQLITE_TOOBIG                      ? Status::Overflow
                                                        : Status::StorageError;
}
bool textValid(std::string_view v, std::size_t max) {
    if (v.empty() || v.size() > max)
        return false;
    for (unsigned char c : v)
        if (c < 0x20 || c == 0x7f)
            return false;
    return true;
}
void verifyDatabase(sqlite3 *db) {
    Statement integrity(db, "PRAGMA integrity_check");
    if (!integrity.row() || integrity.string(0) != "ok")
        throw SqlError(SQLITE_CORRUPT, "Commerce database integrity_check failed");
    Statement foreign(db, "PRAGMA foreign_key_check");
    if (foreign.row())
        throw SqlError(SQLITE_CONSTRAINT, "Commerce database foreign_key_check failed");
}
} // namespace

struct Commerce::Impl {
    sqlite3 *db{};
    std::mutex mutex;

    explicit Impl(std::string path) {
        if (path.empty())
            throw std::invalid_argument("Invalid Commerce DB path");
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
                throw std::runtime_error("Unsupported newer Commerce schema");
            if (version == 0) {
                Statement q(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND "
                                "name NOT LIKE 'sqlite_%'");
                q.row();
                if (q.integer(0) != 0)
                    throw std::runtime_error(
                        "Refusing unrecognized database; only Commerce schema may be migrated");
            } else {
                Statement q(db, "SELECT checksum FROM schema_migration WHERE version=1");
                if (!q.row() || q.string(0) != detail::sha256(detail::schemaV1))
                    throw std::runtime_error("Commerce schema migration checksum mismatch");
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
                    execute(db, "SELECT 1"); // Existing DB must already be at version 1.
                execute(db, "PRAGMA foreign_keys=OFF");
                Transaction tx(db);
                if (version == 0) {
                    execute(db, std::string(detail::schemaV1).c_str());
                    Statement migration(
                        db, "INSERT INTO schema_migration(version,checksum,appliedAt) "
                            "VALUES(1,?,?)");
                    migration.text(1, detail::sha256(detail::schemaV1));
                    migration.number(2, now());
                    migration.done();
                }
                tx.commit();
                execute(db, "PRAGMA foreign_keys=ON");
                verifyDatabase(db);
            }
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
};

Commerce::Commerce(std::string dbPath) : impl_(std::make_unique<Impl>(std::move(dbPath))) {}
Commerce::~Commerce() = default;

GiftResult Commerce::createGift(std::string_view creatorUuid, std::int64_t shareMinor,
                                std::int64_t recipients, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(creatorUuid, 64) || shareMinor < giftMinShareMinor ||
            recipients < giftMinRecipients || recipients > giftMaxRecipients || nowMs < 0)
            return {Status::Invalid, std::nullopt, "Invalid gift parameters"};
        if (shareMinor > giftMaxTotalMinor / recipients)
            return {Status::Overflow, std::nullopt, "Gift total exceeds maximum"};
        const auto totalMinor = shareMinor * recipients;
        if (totalMinor > giftMaxTotalMinor)
            return {Status::Invalid, std::nullopt, "Gift total exceeds maximum"};
        const auto feeMinor = totalMinor * giftFeePermille / 1000;
        const auto expiresAtMs = nowMs + giftLifetimeMs;
        Transaction tx(impl_->db);
        Statement q(impl_->db,
                    "INSERT INTO gifts(creatorUuid,shareMinor,recipients,totalMinor,feeMinor,"
                    "expiresAtMs,status,createdAtMs) VALUES(?,?,?,?,?,?,'open',?)");
        q.text(1, creatorUuid);
        q.number(2, shareMinor);
        q.number(3, recipients);
        q.number(4, totalMinor);
        q.number(5, feeMinor);
        q.number(6, expiresAtMs);
        q.number(7, nowMs);
        q.done();
        const auto id = sqlite3_last_insert_rowid(impl_->db);
        tx.commit();
        return {Status::Ok,
                Gift{id, std::string(creatorUuid), shareMinor, recipients, totalMinor, feeMinor,
                     expiresAtMs, "open", nowMs},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

GiftResult Commerce::gift(std::int64_t id) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0)
            return {Status::Invalid, std::nullopt, "Invalid gift id"};
        Statement q(impl_->db,
                    "SELECT id,creatorUuid,shareMinor,recipients,totalMinor,feeMinor,expiresAtMs,"
                    "status,createdAtMs FROM gifts WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return {Status::NotFound, std::nullopt, "Gift not found"};
        return {Status::Ok,
                Gift{q.integer(0), q.string(1), q.integer(2), q.integer(3), q.integer(4),
                     q.integer(5), q.integer(6), q.string(7), q.integer(8)},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

Status Commerce::claimGift(std::int64_t id, std::string_view playerUuid, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || !textValid(playerUuid, 64) || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db,
                    "SELECT shareMinor,recipients,expiresAtMs,status FROM gifts WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        const auto shareMinor = q.integer(0);
        const auto recipients = q.integer(1);
        const auto expiresAtMs = q.integer(2);
        const auto status = q.string(3);
        if (status != "open")
            return Status::Conflict;
        if (nowMs >= expiresAtMs)
            return Status::Expired;
        Statement claimed(impl_->db, "SELECT count(*) FROM gift_participants WHERE giftId=?");
        claimed.number(1, id);
        claimed.row();
        if (claimed.integer(0) >= recipients)
            return Status::Conflict;
        Statement insert(impl_->db,
                         "INSERT INTO gift_participants(giftId,playerUuid,shareMinor,claimedAtMs) "
                         "VALUES(?,?,?,?)");
        insert.number(1, id);
        insert.text(2, playerUuid);
        insert.number(3, shareMinor);
        insert.number(4, nowMs);
        insert.done();
        Statement after(impl_->db, "SELECT count(*) FROM gift_participants WHERE giftId=?");
        after.number(1, id);
        after.row();
        if (after.integer(0) >= recipients) {
            Statement settle(impl_->db, "UPDATE gifts SET status='settled' WHERE id=?");
            settle.number(1, id);
            settle.done();
        }
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::expireGift(std::int64_t id, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db, "SELECT expiresAtMs,status FROM gifts WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        if (q.string(1) != "open")
            return Status::Conflict;
        if (nowMs < q.integer(0))
            return Status::Ok;
        Statement update(impl_->db, "UPDATE gifts SET status='expired' WHERE id=?");
        update.number(1, id);
        update.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

} // namespace eternal::commerce
