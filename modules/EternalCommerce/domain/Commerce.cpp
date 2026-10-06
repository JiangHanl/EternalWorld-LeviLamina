#include "Commerce.hpp"
#include "Schema.hpp"
#include "Sha256.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
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
std::int64_t dayOf(std::int64_t nowMs) {
    constexpr std::int64_t dayMs = 24ll * 3600 * 1000;
    constexpr std::int64_t utc8OffsetMs = 8ll * 3600 * 1000;
    return (nowMs + utc8OffsetMs) / dayMs;
}
} // namespace

std::int64_t computeTieredTax(std::int64_t taxableMinor, std::span<const TaxTier> tiers) {
    if (taxableMinor <= 0)
        return 0;
    std::int64_t remaining = taxableMinor;
    std::int64_t lower = 0;
    std::int64_t tax = 0;
    for (const auto &tier : tiers) {
        if (remaining <= 0)
            break;
        const auto upper =
            tier.upToMinor > 0 ? tier.upToMinor : std::numeric_limits<std::int64_t>::max();
        if (lower >= upper) {
            lower = upper;
            continue;
        }
        const auto bracket = std::min(remaining, upper - lower);
        tax += bracket * tier.ratePermille / 1000;
        remaining -= bracket;
        lower = upper;
    }
    return tax;
}

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
                if (version >= 2) {
                    Statement v2(db, "SELECT checksum FROM schema_migration WHERE version=2");
                    if (!v2.row() || v2.string(0) != detail::sha256(detail::schemaV2))
                        throw std::runtime_error("Commerce schema migration checksum mismatch");
                }
                if (version >= 3) {
                    Statement v3(db, "SELECT checksum FROM schema_migration WHERE version=3");
                    if (!v3.row() || v3.string(0) != detail::sha256(detail::schemaV3))
                        throw std::runtime_error("Commerce schema migration checksum mismatch");
                }
                if (version >= 4) {
                    Statement v4(db, "SELECT checksum FROM schema_migration WHERE version=4");
                    if (!v4.row() || v4.string(0) != detail::sha256(detail::schemaV4))
                        throw std::runtime_error("Commerce schema migration checksum mismatch");
                }
                if (version >= 5) {
                    Statement v5(db, "SELECT checksum FROM schema_migration WHERE version=5");
                    if (!v5.row() || v5.string(0) != detail::sha256(detail::schemaV5))
                        throw std::runtime_error("Commerce schema migration checksum mismatch");
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
                execute(db, std::string(detail::schemaV2).c_str());
                Statement migration2(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) "
                        "VALUES(2,?,?)");
                migration2.text(1, detail::sha256(detail::schemaV2));
                migration2.number(2, now());
                migration2.done();
                execute(db, std::string(detail::schemaV3).c_str());
                Statement migration3(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) "
                        "VALUES(3,?,?)");
                migration3.text(1, detail::sha256(detail::schemaV3));
                migration3.number(2, now());
                migration3.done();
                execute(db, std::string(detail::schemaV4).c_str());
                Statement migration4(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) "
                        "VALUES(4,?,?)");
                migration4.text(1, detail::sha256(detail::schemaV4));
                migration4.number(2, now());
                migration4.done();
                execute(db, std::string(detail::schemaV5).c_str());
                Statement migration5(
                    db, "INSERT INTO schema_migration(version,checksum,appliedAt) "
                        "VALUES(5,?,?)");
                migration5.text(1, detail::sha256(detail::schemaV5));
                migration5.number(2, now());
                migration5.done();
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

TransferResult Commerce::recordTransfer(std::string_view senderUuid, std::int64_t amountMinor,
                                        std::string_view idempotencyKey, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(senderUuid, 64) || amountMinor <= 0 || !textValid(idempotencyKey, 160) ||
            nowMs < 0)
            return {Status::Invalid, 0, 0, 0, 0, 0, false, "Invalid transfer parameters"};
        const auto day = dayOf(nowMs);
        Transaction tx(impl_->db);
        Statement existing(impl_->db,
                           "SELECT id,taxFreeMinor,taxableMinor,taxMinor,netMinor FROM transfers "
                           "WHERE senderUuid=? AND idempotencyKey=?");
        existing.text(1, senderUuid);
        existing.text(2, idempotencyKey);
        if (existing.row()) {
            TransferResult replayed{Status::Ok, existing.integer(0), existing.integer(1),
                                    existing.integer(2), existing.integer(3), existing.integer(4),
                                    true, ""};
            tx.commit();
            return replayed;
        }
        std::int64_t used = 0;
        Statement allowance(impl_->db,
                            "SELECT exemptUsedMinor FROM transfer_allowance WHERE playerUuid=? "
                            "AND day=?");
        allowance.text(1, senderUuid);
        allowance.number(2, day);
        if (allowance.row())
            used = allowance.integer(0);
        const auto remaining = std::max<std::int64_t>(0, transferDailyExemptMinor - used);
        const auto taxFree = std::min(amountMinor, remaining);
        const auto taxable = amountMinor - taxFree;
        const auto tax = computeTieredTax(taxable, transferDefaultTiers);
        const auto net = amountMinor - tax;
        Statement upsert(impl_->db,
                         "INSERT INTO transfer_allowance(playerUuid,day,exemptUsedMinor) "
                         "VALUES(?,?,?) ON CONFLICT(playerUuid,day) DO UPDATE SET "
                         "exemptUsedMinor=exemptUsedMinor+excluded.exemptUsedMinor");
        upsert.text(1, senderUuid);
        upsert.number(2, day);
        upsert.number(3, taxFree);
        upsert.done();
        Statement insert(impl_->db,
                         "INSERT INTO transfers(senderUuid,amountMinor,taxFreeMinor,taxableMinor,"
                         "taxMinor,netMinor,idempotencyKey,day,createdAtMs) "
                         "VALUES(?,?,?,?,?,?,?,?,?)");
        insert.text(1, senderUuid);
        insert.number(2, amountMinor);
        insert.number(3, taxFree);
        insert.number(4, taxable);
        insert.number(5, tax);
        insert.number(6, net);
        insert.text(7, idempotencyKey);
        insert.number(8, day);
        insert.number(9, nowMs);
        insert.done();
        const auto id = sqlite3_last_insert_rowid(impl_->db);
        tx.commit();
        return {Status::Ok, id, taxFree, taxable, tax, net, false, ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), 0, 0, 0, 0, 0, false, e.what()};
    }
}

TransferResult Commerce::previewTransfer(std::string_view senderUuid, std::int64_t amountMinor,
                                         std::int64_t nowMs) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(senderUuid, 64) || amountMinor <= 0 || nowMs < 0)
            return {Status::Invalid, 0, 0, 0, 0, 0, false, "Invalid transfer parameters"};
        const auto day = dayOf(nowMs);
        std::int64_t used = 0;
        Statement allowance(impl_->db,
                            "SELECT exemptUsedMinor FROM transfer_allowance WHERE playerUuid=? "
                            "AND day=?");
        allowance.text(1, senderUuid);
        allowance.number(2, day);
        if (allowance.row())
            used = allowance.integer(0);
        const auto remaining = std::max<std::int64_t>(0, transferDailyExemptMinor - used);
        const auto taxFree = std::min(amountMinor, remaining);
        const auto taxable = amountMinor - taxFree;
        const auto tax = computeTieredTax(taxable, transferDefaultTiers);
        const auto net = amountMinor - tax;
        return {Status::Ok, 0, taxFree, taxable, tax, net, false, ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), 0, 0, 0, 0, 0, false, e.what()};
    }
}

ConsignmentResult Commerce::listConsignment(std::string_view sellerUuid, std::string_view item,
                                            std::string_view nbt, std::string_view displayName,
                                            std::int64_t priceMinor, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(sellerUuid, 64) || !textValid(item, 256) || !textValid(nbt, 1048576) ||
            !textValid(displayName, 256) || priceMinor <= 0 || nowMs < 0)
            return {Status::Invalid, std::nullopt, "Invalid consignment parameters"};
        const auto expiresAtMs = nowMs + consignmentLifetimeMs;
        Transaction tx(impl_->db);
        Statement insert(impl_->db,
                         "INSERT INTO consignments(sellerUuid,item,nbt,displayName,priceMinor,"
                         "listedAtMs,expiresAtMs,status) VALUES(?,?,?,?,?,?,?,'listed')");
        insert.text(1, sellerUuid);
        insert.text(2, item);
        insert.text(3, nbt);
        insert.text(4, displayName);
        insert.number(5, priceMinor);
        insert.number(6, nowMs);
        insert.number(7, expiresAtMs);
        insert.done();
        const auto id = sqlite3_last_insert_rowid(impl_->db);
        tx.commit();
        return {Status::Ok,
                Consignment{id, std::string(sellerUuid), std::string(item), std::string(nbt),
                            std::string(displayName), priceMinor, nowMs, expiresAtMs, "listed",
                            "", 0},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

ConsignmentResult Commerce::consignment(std::int64_t id) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0)
            return {Status::Invalid, std::nullopt, "Invalid consignment id"};
        Statement q(impl_->db,
                    "SELECT id,sellerUuid,item,nbt,displayName,priceMinor,listedAtMs,expiresAtMs,"
                    "status,coalesce(buyerUuid,''),coalesce(soldAtMs,0) FROM consignments "
                    "WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return {Status::NotFound, std::nullopt, "Consignment not found"};
        return {Status::Ok,
                Consignment{q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                            q.integer(5), q.integer(6), q.integer(7), q.string(8), q.string(9),
                            q.integer(10)},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

Status Commerce::buyConsignment(std::int64_t id, std::string_view buyerUuid, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || !textValid(buyerUuid, 64) || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db,
                    "SELECT sellerUuid,status,expiresAtMs FROM consignments WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        if (q.string(1) != "listed")
            return Status::Conflict;
        if (nowMs >= q.integer(2))
            return Status::Expired;
        if (q.string(0) == buyerUuid)
            return Status::Conflict;
        Statement buy(impl_->db,
                      "UPDATE consignments SET status='sold',buyerUuid=?,soldAtMs=? WHERE id=? "
                      "AND status='listed'");
        buy.text(1, buyerUuid);
        buy.number(2, nowMs);
        buy.number(3, id);
        buy.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::cancelConsignment(std::int64_t id, std::string_view sellerUuid,
                                   std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || !textValid(sellerUuid, 64) || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db,
                    "SELECT sellerUuid,status,expiresAtMs FROM consignments WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        if (q.string(1) != "listed")
            return Status::Conflict;
        if (q.string(0) != sellerUuid)
            return Status::Conflict;
        Statement update(impl_->db,
                         "UPDATE consignments SET status='cancelled' WHERE id=? AND "
                         "status='listed'");
        update.number(1, id);
        update.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::returnExpiredConsignment(std::int64_t id, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db, "SELECT status,expiresAtMs FROM consignments WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        if (q.string(0) != "listed")
            return Status::Conflict;
        if (nowMs < q.integer(1))
            return Status::Ok;
        Statement update(impl_->db,
                         "UPDATE consignments SET status='returned' WHERE id=? AND "
                         "status='listed'");
        update.number(1, id);
        update.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

AcquisitionResult Commerce::requestAcquisition(std::string_view requesterUuid,
                                               std::string_view item, std::int64_t amountMinor,
                                               std::string_view idempotencyKey,
                                               std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(requesterUuid, 64) || !textValid(item, 256) || amountMinor <= 0 ||
            !textValid(idempotencyKey, 160) || nowMs < 0)
            return {Status::Invalid, std::nullopt, 0, false, "Invalid acquisition parameters"};
        const auto day = dayOf(nowMs);
        Transaction tx(impl_->db);
        Statement existing(impl_->db,
                           "SELECT id,item,amountMinor,status,createdAtMs FROM acquisitions "
                           "WHERE requesterUuid=? AND idempotencyKey=?");
        existing.text(1, requesterUuid);
        existing.text(2, idempotencyKey);
        std::int64_t used = 0;
        Statement q(impl_->db,
                    "SELECT usedMinor FROM acquisition_quota WHERE playerUuid=? AND day=?");
        q.text(1, requesterUuid);
        q.number(2, day);
        if (q.row())
            used = q.integer(0);
        const auto remaining = std::max<std::int64_t>(0, acquisitionDailyQuotaMinor - used);
        if (existing.row()) {
            Acquisition acq{existing.integer(0), std::string(requesterUuid), existing.string(1),
                            existing.integer(2), existing.string(3), existing.integer(4)};
            tx.commit();
            return {Status::Ok, acq, remaining, true, ""};
        }
        if (amountMinor > remaining)
            return {Status::Conflict, std::nullopt, remaining, false,
                    "Acquisition quota exceeded"};
        Statement upsert(impl_->db,
                         "INSERT INTO acquisition_quota(playerUuid,day,usedMinor) VALUES(?,?,?) "
                         "ON CONFLICT(playerUuid,day) DO UPDATE SET "
                         "usedMinor=usedMinor+excluded.usedMinor");
        upsert.text(1, requesterUuid);
        upsert.number(2, day);
        upsert.number(3, amountMinor);
        upsert.done();
        Statement insert(impl_->db,
                         "INSERT INTO acquisitions(requesterUuid,item,amountMinor,idempotencyKey,"
                         "day,status,createdAtMs) VALUES(?,?,?,?,?,?,?)");
        insert.text(1, requesterUuid);
        insert.text(2, item);
        insert.number(3, amountMinor);
        insert.text(4, idempotencyKey);
        insert.number(5, day);
        insert.text(6, "open");
        insert.number(7, nowMs);
        insert.done();
        const auto id = sqlite3_last_insert_rowid(impl_->db);
        tx.commit();
        return {Status::Ok,
                Acquisition{id, std::string(requesterUuid), std::string(item), amountMinor, "open",
                            nowMs},
                remaining - amountMinor, false, ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, 0, false, e.what()};
    }
}

std::int64_t Commerce::remainingAcquisitionQuota(std::string_view requesterUuid,
                                                 std::int64_t nowMs) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(requesterUuid, 64) || nowMs < 0)
            return 0;
        const auto day = dayOf(nowMs);
        std::int64_t used = 0;
        Statement q(impl_->db,
                    "SELECT usedMinor FROM acquisition_quota WHERE playerUuid=? AND day=?");
        q.text(1, requesterUuid);
        q.number(2, day);
        if (q.row())
            used = q.integer(0);
        return std::max<std::int64_t>(0, acquisitionDailyQuotaMinor - used);
    } catch (const SqlError &) {
        return 0;
    }
}

Status Commerce::cancelAcquisition(std::int64_t id, std::string_view requesterUuid,
                                   std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || !textValid(requesterUuid, 64) || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement q(impl_->db,
                    "SELECT requesterUuid,amountMinor,status,day FROM acquisitions WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return Status::NotFound;
        if (q.string(0) != requesterUuid || q.string(2) != "open")
            return Status::Conflict;
        const auto amountMinor = q.integer(1);
        const auto day = q.integer(3);
        Statement update(impl_->db,
                         "UPDATE acquisitions SET status='cancelled' WHERE id=? AND "
                         "status='open'");
        update.number(1, id);
        update.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        Statement refund(impl_->db,
                         "UPDATE acquisition_quota SET usedMinor=usedMinor-? WHERE "
                         "playerUuid=? AND day=?");
        refund.number(1, amountMinor);
        refund.text(2, requesterUuid);
        refund.number(3, day);
        refund.done();
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

DeliveryResult Commerce::createDelivery(std::string_view playerUuid, std::string_view item,
                                        std::string_view nbt, std::string_view source,
                                        std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(playerUuid, 64) || !textValid(item, 256) || !textValid(nbt, 1048576) ||
            !textValid(source, 64) || nowMs < 0)
            return {Status::Invalid, std::nullopt, "Invalid delivery parameters"};
        Transaction tx(impl_->db);
        Statement insert(impl_->db,
                         "INSERT INTO delivery_requests(playerUuid,item,nbt,source,status,"
                         "createdAtMs,updatedAtMs) VALUES(?,?,?,?,'pending',?,?)");
        insert.text(1, playerUuid);
        insert.text(2, item);
        insert.text(3, nbt);
        insert.text(4, source);
        insert.number(5, nowMs);
        insert.number(6, nowMs);
        insert.done();
        const auto id = sqlite3_last_insert_rowid(impl_->db);
        tx.commit();
        return {Status::Ok,
                Delivery{id, std::string(playerUuid), std::string(item), std::string(nbt),
                         std::string(source), "pending", 0, nowMs, nowMs, ""},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

DeliveryResult Commerce::delivery(std::int64_t id) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0)
            return {Status::Invalid, std::nullopt, "Invalid delivery id"};
        Statement q(impl_->db,
                    "SELECT id,playerUuid,item,nbt,source,status,attemptCount,createdAtMs,"
                    "updatedAtMs,lastError FROM delivery_requests WHERE id=?");
        q.number(1, id);
        if (!q.row())
            return {Status::NotFound, std::nullopt, "Delivery not found"};
        return {Status::Ok,
                Delivery{q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                         q.string(5), q.integer(6), q.integer(7), q.integer(8), q.string(9)},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

DeliveryResult Commerce::deliveryBySource(std::string_view source) const {
    std::lock_guard lock(impl_->mutex);
    try {
        if (!textValid(source, 64))
            return {Status::Invalid, std::nullopt, "Invalid delivery source"};
        Statement q(impl_->db,
                    "SELECT id,playerUuid,item,nbt,source,status,attemptCount,createdAtMs,"
                    "updatedAtMs,lastError FROM delivery_requests WHERE source=? LIMIT 1");
        q.text(1, source);
        if (!q.row())
            return {Status::NotFound, std::nullopt, "Delivery not found"};
        return {Status::Ok,
                Delivery{q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                         q.string(5), q.integer(6), q.integer(7), q.integer(8), q.string(9)},
                ""};
    } catch (const SqlError &e) {
        return {sqlStatus(e), std::nullopt, e.what()};
    }
}

Status Commerce::beginDelivery(std::int64_t id, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement update(impl_->db,
                         "UPDATE delivery_requests SET status='delivering',"
                         "attemptCount=attemptCount+1,updatedAtMs=? WHERE id=? AND "
                         "status='pending'");
        update.number(1, nowMs);
        update.number(2, id);
        update.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::completeDelivery(std::int64_t id, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement update(impl_->db,
                         "UPDATE delivery_requests SET status='delivered',updatedAtMs=? WHERE "
                         "id=? AND status='delivering'");
        update.number(1, nowMs);
        update.number(2, id);
        update.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::failDelivery(std::int64_t id, std::string_view error, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || !textValid(error, 512) || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement update(impl_->db,
                         "UPDATE delivery_requests SET status='reconciling',lastError=?,"
                         "updatedAtMs=? WHERE id=? AND status='delivering'");
        update.text(1, error);
        update.number(2, nowMs);
        update.number(3, id);
        update.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

Status Commerce::reconcileDelivery(std::int64_t id, std::int64_t nowMs) {
    std::lock_guard lock(impl_->mutex);
    try {
        if (id <= 0 || nowMs < 0)
            return Status::Invalid;
        Transaction tx(impl_->db);
        Statement update(impl_->db,
                         "UPDATE delivery_requests SET status='pending',updatedAtMs=? WHERE "
                         "id=? AND status='reconciling'");
        update.number(1, nowMs);
        update.number(2, id);
        update.done();
        if (sqlite3_changes(impl_->db) != 1)
            return Status::Conflict;
        tx.commit();
        return Status::Ok;
    } catch (const SqlError &e) {
        return sqlStatus(e);
    }
}

std::vector<Delivery> Commerce::pendingDeliveries(std::size_t limit) const {
    std::lock_guard lock(impl_->mutex);
    if (limit == 0 || limit > 1000)
        throw std::invalid_argument("Delivery limit must be 1..1000");
    std::vector<Delivery> result;
    Statement q(impl_->db,
                "SELECT id,playerUuid,item,nbt,source,status,attemptCount,createdAtMs,"
                "updatedAtMs,lastError FROM delivery_requests WHERE status='pending' ORDER BY "
                "id LIMIT ?");
    q.number(1, static_cast<std::int64_t>(limit));
    while (q.row())
        result.push_back(Delivery{q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                                  q.string(5), q.integer(6), q.integer(7), q.integer(8),
                                  q.string(9)});
    return result;
}

std::vector<Delivery> Commerce::reconcilingDeliveries(std::size_t limit) const {
    std::lock_guard lock(impl_->mutex);
    if (limit == 0 || limit > 1000)
        throw std::invalid_argument("Delivery limit must be 1..1000");
    std::vector<Delivery> result;
    Statement q(impl_->db,
                "SELECT id,playerUuid,item,nbt,source,status,attemptCount,createdAtMs,"
                "updatedAtMs,lastError FROM delivery_requests WHERE status='reconciling' ORDER "
                "BY id LIMIT ?");
    q.number(1, static_cast<std::int64_t>(limit));
    while (q.row())
        result.push_back(Delivery{q.integer(0), q.string(1), q.string(2), q.string(3), q.string(4),
                                  q.string(5), q.integer(6), q.integer(7), q.integer(8),
                                  q.string(9)});
    return result;
}

} // namespace eternal::commerce
