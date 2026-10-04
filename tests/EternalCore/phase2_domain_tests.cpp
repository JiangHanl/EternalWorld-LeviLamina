#include "Core.hpp"
#include "Schema.hpp"
#include "Sha256.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>

using namespace eternal::core;
namespace fs = std::filesystem;
namespace {
const VerifiedIdentity ownerIdentity{"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", "1000", "Owner"};
const VerifiedIdentity aliceIdentity{"bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", "1001", "Traveler"};
const VerifiedIdentity bobIdentity{"cccccccc-cccc-cccc-cccc-cccccccccccc", "1002", "Traveler"};
int groups{};
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void equal(Status actual, Status expected) {
    require(actual == expected,
            std::string("Expected ") + name(expected) + ", got " + name(actual));
}
template <class F> void test(const std::string &label, F fn) {
    fn();
    ++groups;
    std::cout << "PASS " << label << '\n';
}
Actor actor(Core &core, const VerifiedIdentity &identity) {
    auto result = core.ensurePlayer(identity);
    equal(result.status, Status::Ok);
    require(result.actor.has_value(), "Missing actor");
    return *result.actor;
}
struct Db {
    sqlite3 *db{};
    explicit Db(const fs::path &path, bool write = false) {
        if (sqlite3_open_v2(path.string().c_str(), &db,
                            write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                  : SQLITE_OPEN_READONLY,
                            nullptr) != SQLITE_OK)
            throw std::runtime_error("Test DB open failed");
    }
    ~Db() { sqlite3_close(db); }
    void exec(const std::string &sql) {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    std::string query(const std::string &sql) {
        sqlite3_stmt *q{};
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &q, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        const auto rc = sqlite3_step(q);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            sqlite3_finalize(q);
            throw std::runtime_error(sqlite3_errmsg(db));
        }
        const auto *value = sqlite3_column_text(q, 0);
        std::string result = value ? reinterpret_cast<const char *>(value) : "";
        sqlite3_finalize(q);
        return result;
    }
};
std::string read(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Test file missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::string sqlQuote(std::string_view value) {
    std::string out = "'";
    for (char c : value) {
        out += c;
        if (c == '\'')
            out += '\'';
    }
    return out + '\'';
}
std::string canonical(std::initializer_list<std::string_view> values) {
    std::string out = "eternal-core-request-v1|";
    for (auto v : values)
        out += std::to_string(v.size()) + ":" + std::string(v) + "|";
    return out;
}
std::int64_t balance(Core &core, const Actor &who, Asset asset = Asset::Coin) {
    auto result = core.balance(who.uuid(), asset);
    equal(result.status, Status::Ok);
    return result.amount;
}
std::vector<fs::path> backups(const fs::path &db) {
    std::vector<fs::path> result;
    for (auto &entry : fs::directory_iterator(db.parent_path())) {
        auto n = entry.path().filename().string();
        if (n.starts_with(db.filename().string() + ".pre-migration-") &&
            entry.path().extension() == ".sqlite")
            result.push_back(entry.path());
    }
    return result;
}
void makeV1(const fs::path &path) {
    Db db(path, true);
    db.exec(std::string(detail::schemaV1));
    db.exec(
        "INSERT INTO metadata VALUES('ownerXuid','1000');INSERT INTO schema_migration VALUES(1," +
        sqlQuote(detail::sha256(detail::schemaV1)) + ",1)");
    for (const auto &identity : {ownerIdentity, aliceIdentity, bobIdentity}) {
        db.exec("INSERT INTO players VALUES(" + sqlQuote(identity.uuid) + "," +
                sqlQuote(identity.xuid) + ");INSERT INTO accounts VALUES(" +
                sqlQuote(identity.uuid) + ",'coin',0),(" + sqlQuote(identity.uuid) +
                ",'reputation',0)");
    }
    const auto payload = canonical({"grant", "core.domain.v1", ownerIdentity.uuid, "1000",
                                    aliceIdentity.uuid, "coin", "100", "old grant"});
    const auto hash = detail::sha256(payload);
    db.exec("UPDATE accounts SET balance=100 WHERE uuid=" + sqlQuote(aliceIdentity.uuid) +
            " AND asset='coin'");
    db.exec("INSERT INTO transactions VALUES('tx_old','core.domain.v1','old-key','committed',1,1," +
            sqlQuote(payload) + "," + sqlQuote(hash) + ",'grant'," + sqlQuote(ownerIdentity.uuid) +
            ",0)");
    db.exec("INSERT INTO entries(transactionId,uuid,asset,delta,balanceAfter) VALUES('tx_old'," +
            sqlQuote(aliceIdentity.uuid) + ",'coin',100,100)");
    db.exec(
        "INSERT INTO receipts VALUES('tx_old'," + sqlQuote(hash) +
        ",'{\"transactionId\":\"tx_old\",\"status\":\"committed\"}');INSERT INTO "
        "audit(transactionId,actorUuid,operation,reason,requestHash,createdAt) VALUES('tx_old'," +
        sqlQuote(ownerIdentity.uuid) + ",'grant','old grant'," + sqlQuote(hash) + ",1)");
    db.exec("INSERT INTO roles VALUES(" + sqlQuote(aliceIdentity.uuid) +
            ",'build');INSERT INTO outbox(transactionId,targetUuid,eventType,payload,createdAt) "
            "VALUES('tx_old'," +
            sqlQuote(aliceIdentity.uuid) + ",'balance_changed','{}',1)");
}
struct InjectedFault : std::runtime_error {
    InjectedFault() : runtime_error("Injected exception, not process crash") {}
};
std::string commandQuote(const std::string &text) {
    require(text.find_first_of("\"\r\n&|<>") == std::string::npos, "Unsafe test child path");
    return '"' + text + '"';
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--crash") {
        const int point = std::stoi(argv[3]);
        Core core(argv[2], "1000", [point](FaultPoint p) {
            if (static_cast<int>(p) == point)
                std::_Exit(87);
        });
        const auto owner = core.restoreActor(ownerIdentity.uuid);
        if (!owner.actor)
            return 3;
        core.adjustAsset(*owner.actor, aliceIdentity.uuid, Asset::Coin, -25, "crash adjustment",
                         "crash-adjust");
        return 2;
    }
    try {
        const auto suffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path root = fs::temp_directory_path() / ("EternalCorePhase2Tests-" + suffix);
        fs::create_directories(root);
        std::cout << "TEST_DIRECTORY " << root.string() << '\n';
        test("002 migration source exactly equals embedded SQL", [&] {
            const auto sql = fs::path(__FILE__).parent_path().parent_path().parent_path() /
                             "migrations/EternalCore/002_identity_permissions_delivery.sql";
            require(read(sql) == detail::schemaV2, "002 SQL differs from embedded schema");
        });
        test("stable private identity and display metadata survive restart", [&] {
            const auto path = root / "identity.sqlite";
            std::string playerId;
            {
                Core core(path.string(), "1000");
                const auto alice = actor(core, aliceIdentity);
                playerId = alice.playerId();
                const auto p = *core.player(playerId).player;
                require(p.displayName == "Traveler" && p.firstSeen > 0 &&
                            p.lastSeen >= p.firstSeen && p.identityVersion == 1,
                        "Identity metadata invalid");
                require(core.restoreActor(playerId).actor->playerId() == playerId,
                        "Player ID restore failed");
            }
            Core core(path.string(), "1000");
            const auto restored = core.restoreActor(playerId);
            equal(restored.status, Status::Ok);
            require(restored.actor->uuid() == aliceIdentity.uuid, "Restore changed identity");
            require(core.player(playerId).player->displayName == "Traveler", "Restore erased name");
        });
        test("same display names coexist, rename never rebinds or authorizes", [&] {
            Core core((root / "names.sqlite").string(), "1000");
            const auto a = actor(core, aliceIdentity), b = actor(core, bobIdentity);
            equal(core.findPlayerByDisplayName("Traveler").status, Status::Conflict);
            const auto first = core.player(a.uuid()).player->firstSeen;
            equal(core.ensurePlayer({a.uuid(), a.xuid(), "Renamed"}).status, Status::Ok);
            const auto p = *core.player(a.uuid()).player;
            require(p.playerId == a.playerId() && p.firstSeen == first && p.identityVersion == 1,
                    "Rename changed binding");
            equal(core.findPlayerByDisplayName("Renamed").status, Status::Ok);
            equal(core.ensurePlayer({a.uuid(), b.xuid(), "Owner"}).status, Status::Conflict);
            require(!core.hasRole(a, Role::Owner), "Display name granted ownership");
        });
        test("identity rejects malformed UTF8 and noncanonical XUID", [&] {
            Core core((root / "invalid-identity.sqlite").string(), "1000");
            for (const auto &name : {std::string("bad\nname"), std::string("\xc0\xaf", 2),
                                     std::string("\xed\xa0\x80", 3), std::string(129, 'x')})
                equal(core.ensurePlayer({aliceIdentity.uuid, "1001", name}).status,
                      Status::Invalid);
            equal(core.ensurePlayer({aliceIdentity.uuid, "01001", "Traveler"}).status,
                  Status::Invalid);
            equal(
                core.ensurePlayer({aliceIdentity.uuid, "18446744073709551616", "Traveler"}).status,
                Status::Invalid);
            equal(
                core.ensurePlayer({"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB", "1001", "旅人"}).status,
                Status::Ok);
        });
        test("role permission matrix is explicit and OP-free", [&] {
            Core core((root / "permissions.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            require(core.hasRole(a, Role::Player) &&
                        !core.hasPermission(a, Permission::MoneyAdjust),
                    "Default player rights wrong");
            require(core.hasPermission(a, Permission::MoneyTransfer) &&
                        core.hasPermission(a, Permission::MoneyRead),
                    "Player self actions missing");
            for (const auto &[role, permission] : std::vector<std::pair<Role, Permission>>{
                     {Role::ContentManager, Permission::ContentManage},
                     {Role::EconomyManager, Permission::MoneyAdjust},
                     {Role::Builder, Permission::WorldBuild},
                     {Role::Moderator, Permission::Moderation},
                     {Role::Admin, Permission::AuditRead}}) {
                const auto key = std::string(name(role));
                equal(core.setRole(owner, a.uuid(), role, true, "role test", key).status,
                      Status::Ok);
                require(core.hasPermission(a, permission), "Assigned permission missing");
                equal(
                    core.setRole(owner, a.uuid(), role, false, "role revoke", key + "-off").status,
                    Status::Ok);
                require(!core.hasPermission(a, permission), "Revoked permission remained");
            }
            require(!core.hasPermission(a, Permission::ReputationAdjust) &&
                        !core.hasPermission(a, Permission::RoleManage),
                    "Player gained elevated rights");
        });
        test("owner cannot be granted or revoked, attempts have durable audit", [&] {
            const auto path = root / "owner.sqlite";
            Core core(path.string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            for (bool enabled : {true, false}) {
                const auto r =
                    core.setRole(owner, a.uuid(), Role::Owner, enabled, "forbidden owner change",
                                 enabled ? "grant-owner" : "remove-owner");
                equal(r.status, Status::PermissionDenied);
                require(!r.receipt.empty(), "Owner denial has no receipt");
            }
            require(core.hasRole(owner, Role::Owner) && !core.hasRole(a, Role::Owner),
                    "Owner changed");
            Db db(path);
            require(db.query("SELECT count(*) FROM audit WHERE resultCode!=0") == "2",
                    "Owner attempts unaudited");
        });
        test("grant and revoke increment target revision; cached actors see live revocation", [&] {
            Core core((root / "revision.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            const auto revision = *core.permissionRevision(a.uuid());
            equal(core.setRole(owner, a.uuid(), Role::EconomyManager, true, "appoint", "on").status,
                  Status::Ok);
            require(*core.permissionRevision(a.uuid()) == revision + 1,
                    "Grant revision did not increment");
            require(core.hasPermission(a, Permission::MoneyAdjust) &&
                        !core.hasPermission(a, Permission::ReputationAdjust),
                    "Economy scope too broad");
            equal(
                core.setRole(owner, a.uuid(), Role::EconomyManager, false, "revoke", "off").status,
                Status::Ok);
            require(*core.permissionRevision(a.uuid()) == revision + 2 &&
                        !core.hasPermission(a, Permission::MoneyAdjust),
                    "Revoke not live");
        });
        test("integer signed coin and reputation changes obey least privilege", [&] {
            Core core((root / "assets.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity),
                       b = actor(core, bobIdentity);
            equal(
                core.setRole(owner, a.uuid(), Role::EconomyManager, true, "economy", "role").status,
                Status::Ok);
            equal(core.adjustAsset(a, b.uuid(), Asset::Coin, 150, "event", "coin-on").status,
                  Status::Ok);
            equal(core.adjustAsset(a, b.uuid(), Asset::Coin, -50, "correction", "coin-off").status,
                  Status::Ok);
            equal(core.adjustAsset(a, b.uuid(), Asset::Reputation, 1, "event", "rep-denied").status,
                  Status::PermissionDenied);
            equal(core.adjustAsset(owner, b.uuid(), Asset::Reputation, 10, "competition", "rep-on")
                      .status,
                  Status::Ok);
            equal(core.adjustAsset(owner, b.uuid(), Asset::Reputation, -3, "correction", "rep-off")
                      .status,
                  Status::Ok);
            require(balance(core, b) == 100 && balance(core, b, Asset::Reputation) == 7,
                    "Asset values wrong");
            equal(core.adjustAsset(owner, b.uuid(), Asset::Coin,
                                   std::numeric_limits<std::int64_t>::min(), "invalid debit", "min")
                      .status,
                  Status::InsufficientFunds);
            equal(core.adjustAsset(owner, b.uuid(), Asset::Coin,
                                   std::numeric_limits<std::int64_t>::max(), "overflow", "max")
                      .status,
                  Status::Overflow);
            require(core.selfcheck().value.ledgerConsistent, "Asset ledger inconsistent");
        });
        test("valid denied request persists exact receipt and stays denied after funding", [&] {
            const auto path = root / "denials.sqlite";
            std::string receipt;
            {
                Core core(path.string(), "1000");
                const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity),
                           b = actor(core, bobIdentity);
                const auto r = core.transfer(a, b.uuid(), 25, "payment", "denied-key");
                equal(r.status, Status::InsufficientFunds);
                receipt = r.receipt;
                require(!receipt.empty(), "Missing denial receipt");
                equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 100, "fund", "fund").status,
                      Status::Ok);
            }
            Core core(path.string(), "1000");
            const auto a = *core.restoreActor(aliceIdentity.uuid).actor;
            const auto replay = core.transfer(a, bobIdentity.uuid, 25, "payment", "denied-key");
            equal(replay.status, Status::InsufficientFunds);
            require(replay.replayed && replay.receipt == receipt, "Denied replay mutated");
            require(balance(core, a) == 100, "Denied replay debited money");
            equal(core.transfer(a, bobIdentity.uuid, 26, "payment", "denied-key").status,
                  Status::Conflict);
        });
        test("module and actor scopes isolate keys; changed business payload conflicts", [&] {
            Core core((root / "scopes.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity),
                       b = actor(core, bobIdentity);
            const DomainRequestMetadata market{"market", "reward", "request-A", 1},
                quest{"quest", "reward", "request-B", 1};
            equal(
                core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward", "same", market).status,
                Status::Ok);
            equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 20, "reward", "same", quest).status,
                  Status::Ok);
            equal(core.transfer(a, b.uuid(), 1, "payment", "same", market).status, Status::Ok);
            equal(
                core.ownerGrant(owner, a.uuid(), Asset::Coin, 11, "reward", "same", market).status,
                Status::Conflict);
            auto renewed = market;
            renewed.permissionRevision = 2;
            require(core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward", "same", renewed)
                        .replayed,
                    "New session revision prevented replay");
            require(core.receipt(owner, "same", market).receipt !=
                        core.receipt(owner, "same", quest).receipt,
                    "Cross-module receipt leaked");
            require(balance(core, a) == 29, "Isolated scopes wrong balance");
        });
        test("request audit carries trusted module action request and permission snapshot", [&] {
            const auto path = root / "attribution.sqlite";
            Core core(path.string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            const DomainRequestMetadata metadata{"events", "event.reward", "req-123", 9};
            equal(
                core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward", "attribution", metadata)
                    .status,
                Status::Ok);
            Db db(path);
            require(
                db.query("SELECT moduleId||':'||operation||':'||requestId||':'||permissionRevision "
                         "FROM audit") == "events:event.reward:req-123:9",
                "Attribution lost");
            require(db.query("SELECT targetUuid FROM audit") == a.uuid(), "Audit target lost");
        });
        test("unidentified runtime denial audit is bounded with durable total aggregation", [&] {
            const auto path = root / "bounded-audit.sqlite";
            Core core(path.string(), "1000");
            DenialRecord denied;
            denied.moduleId = "market";
            denied.action = "money.transfer";
            denied.reason = "Expired context";
            denied.permissionRevision = 4;
            for (int i = 0; i < 100; ++i) {
                denied.requestId = "request-" + std::to_string(i);
                equal(core.auditDenied(denied), Status::Ok);
            }
            Db db(path);
            require(db.query("SELECT count(*) FROM audit") == "64", "Audit detail limit bypassed");
            require(db.query("SELECT totalCount||':'||detailedCount FROM denial_audit_budget") ==
                        "100:64",
                    "Denied aggregate missing");
            require(db.query("SELECT count(*) FROM audit WHERE actorUuid IS NULL") == "64",
                    "Unidentified actor forged");
            require(db.query("SELECT count(*) FROM transactions") == "0",
                    "Runtime denial created assets transaction");
        });
        test("detail audit limit does not discard durable high-value denial receipts", [&] {
            const auto path = root / "bounded-receipts.sqlite";
            Core core(path.string(), "1000");
            const auto a = actor(core, aliceIdentity), b = actor(core, bobIdentity);
            for (int i = 0; i < 70; ++i) {
                const auto key = "denial-" + std::to_string(i);
                const auto result = core.transfer(a, b.uuid(), 1, "unfunded transfer", key);
                equal(result.status, Status::InsufficientFunds);
                require(!result.receipt.empty() && core.receipt(a, key).receipt == result.receipt,
                        "Audit cap discarded receipt");
            }
            Db db(path);
            require(db.query("SELECT count(*) FROM audit") == "64" &&
                        db.query("SELECT count(*) FROM receipts") == "70" &&
                        db.query("SELECT totalCount FROM denial_audit_budget") == "70",
                    "Detailed audit and durable receipt limits were confused");
            require(db.query("SELECT count(*) FROM entries") == "0" &&
                        db.query("SELECT count(*) FROM outbox") == "0",
                    "Denied requests produced asset effects");
        });
        test("runtime context rejection has a durable module-scoped receipt and audited conflict",
             [&] {
                 const auto path = root / "runtime-rejection.sqlite";
                 Core core(path.string(), "1000");
                 const auto a = actor(core, aliceIdentity);
                 const DomainRequestMetadata metadata{"market", "money.transfer", "request-9", 1};
                 const std::string payload = "{\"amount\":25,\"target\":\"offline-player\"}";
                 const auto denied = core.recordRejectedRequest(
                     a, "offline-player", "transfer", payload, "Expired context", "runtime-key",
                     Status::PermissionDenied, metadata);
                 equal(denied.status, Status::PermissionDenied);
                 require(!denied.receipt.empty(), "Runtime denial has no receipt");
                 const auto retry = core.recordRejectedRequest(
                     a, "offline-player", "transfer", payload, "Expired context", "runtime-key",
                     Status::PermissionDenied, metadata);
                 require(retry.replayed && retry.receipt == denied.receipt,
                         "Runtime denied receipt changed");
                 const auto changed = core.recordRejectedRequest(
                     a, "offline-player", "transfer", "{\"amount\":26}", "Changed request",
                     "runtime-key", Status::PermissionDenied, metadata);
                 equal(changed.status, Status::Conflict);
                 require(changed.receipt == denied.receipt, "Conflict replaced original receipt");
                 Db db(path);
                 require(db.query("SELECT count(*) FROM transactions") == "1" &&
                             db.query("SELECT count(*) FROM audit") == "2",
                         "Runtime conflict not audited or tx duplicated");
                 require(db.query("SELECT count(*) FROM entries") == "0" &&
                             db.query("SELECT count(*) FROM outbox") == "0",
                         "Runtime rejection produced side effects");
             });
        test("runtime rejection never overwrites a committed transaction receipt", [&] {
            const auto path = root / "runtime-committed.sqlite";
            Core core(path.string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            const auto original =
                core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "grant", "committed-key");
            equal(original.status, Status::Ok);
            Db db(path);
            const auto payload = db.query("SELECT payload FROM transactions");
            const auto existing =
                core.recordRejectedRequest(owner, a.uuid(), "grant", payload, "Expired context",
                                           "committed-key", Status::PermissionDenied);
            require(existing.status == Status::Ok && existing.replayed &&
                        existing.receipt == original.receipt,
                    "Committed receipt changed");
            const auto changed = core.recordRejectedRequest(
                owner, a.uuid(), "grant", "different canonical payload", "Changed request",
                "committed-key", Status::PermissionDenied);
            equal(changed.status, Status::Conflict);
            require(changed.receipt == original.receipt && balance(core, a) == 10,
                    "Conflict overwrote committed state");
            require(db.query("SELECT count(*) FROM receipts") == "1" &&
                        db.query("SELECT count(*) FROM audit WHERE resultCode!=0") == "1",
                    "Committed conflict attempt missing");
        });
        test("one runtime canonical encoding covers both successful and rejected paths", [&] {
            Core core((root / "runtime-canonical.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity),
                       b = actor(core, bobIdentity);
            DomainRequestMetadata grantMetadata{"events", "reward", "original-request", 1};
            grantMetadata.canonicalRequestPayload = "{\"action\":\"reward\",\"amount\":10}";
            const auto granted = core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward",
                                                 "reward", grantMetadata);
            equal(granted.status, Status::Ok);
            require(granted.receipt.find("\"moduleId\":\"events\"") != std::string::npos &&
                        granted.receipt.find("\"requestId\":\"original-request\"") !=
                            std::string::npos,
                    "Receipt attribution missing");
            auto stale = grantMetadata;
            stale.requestId = "retry-request";
            const auto already = core.recordRejectedRequest(
                owner, a.uuid(), "grant", stale.canonicalRequestPayload, "Expired context",
                "reward", Status::PermissionDenied, stale);
            require(already.status == Status::Ok && already.replayed &&
                        already.receipt == granted.receipt,
                    "Canonical success replay changed attribution");
            DomainRequestMetadata transferMetadata{"market", "money.transfer", "denied-request", 1};
            transferMetadata.canonicalRequestPayload =
                "{\"action\":\"money.transfer\",\"amount\":1}";
            const auto denied = core.recordRejectedRequest(
                a, b.uuid(), "transfer", transferMetadata.canonicalRequestPayload,
                "Revoked context", "transfer", Status::PermissionDenied, transferMetadata);
            equal(denied.status, Status::PermissionDenied);
            const auto retry =
                core.transfer(a, b.uuid(), 1, "payment", "transfer", transferMetadata);
            require(retry.replayed && retry.status == Status::PermissionDenied &&
                        retry.receipt == denied.receipt && balance(core, a) == 10,
                    "Rejected and successful routes used different hashes");
        });
        test("canonical receipt request and authoritative snapshots survive restart", [&] {
            const auto path = root / "receipt-metadata.sqlite";
            struct Stored {
                std::string actorUuid, key, receipt, request;
                Status status;
            };
            std::vector<Stored> stored;
            {
                Core core(path.string(), "1000");
                const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity),
                           b = actor(core, bobIdentity);
                const auto legacy = core.ownerGrant(owner, a.uuid(), Asset::Coin, 100,
                                                    "legacy seed", "legacy-seed");
                require(legacy.receipt.find("\"request\":") == std::string::npos &&
                            legacy.receipt.find("\"createdAt\":") == std::string::npos,
                        "Legacy receipt format changed");
                const auto metadata = [&](std::string key, int operation, int asset,
                                          std::int64_t amount, std::string target,
                                          std::string recipient = "", unsigned role = 0) {
                    DomainRequestMetadata value{"market", "test", key, 1};
                    value.canonicalRequestPayload =
                        "{\"asset\":" + std::to_string(asset) +
                        ",\"expectedRevision\":1,\"minorUnits\":" + std::to_string(amount) +
                        ",\"operation\":" + std::to_string(operation) +
                        ",\"reason\":\"metadata test\",\"recipient\":\"" + recipient +
                        "\",\"roleMask\":" + std::to_string(role) +
                        ",\"target\":\"" + target + "\"}";
                    return value;
                };
                const auto save = [&](const Actor &actor, const DomainRequestMetadata &meta,
                                      Result result, Status expected) {
                    equal(result.status, expected);
                    require(result.receipt.find("\"request\":" + meta.canonicalRequestPayload) !=
                                std::string::npos,
                            "Canonical request changed in receipt");
                    stored.push_back({actor.uuid(), meta.requestId, result.receipt,
                                      meta.canonicalRequestPayload, result.status});
                };
                auto grant = metadata("metadata-grant", 1, 1, 5, a.playerId());
                save(owner, grant, core.ownerGrant(owner, a.uuid(), Asset::Coin, 5,
                                                  "metadata test", grant.requestId, grant),
                     Status::Ok);
                auto adjust = metadata("metadata-adjust", 2, 2, 3, a.playerId());
                save(owner, adjust, core.adjustAsset(owner, a.uuid(), Asset::Reputation, 3,
                                                     "metadata test", adjust.requestId, adjust),
                     Status::Ok);
                auto transfer = metadata("metadata-transfer", 3, 1, 10, a.playerId(), b.playerId());
                save(a, transfer, core.transfer(a, b.uuid(), 10, "metadata test",
                                                transfer.requestId, transfer), Status::Ok);
                auto role = metadata("metadata-role", 4, 0, 0, a.playerId(), "", 1);
                save(owner, role, core.setRole(owner, a.uuid(), Role::Build, true,
                                               "metadata test", role.requestId, role), Status::Ok);
                auto denied = metadata("metadata-denied", 3, 1, 999, b.playerId(), a.playerId());
                save(b, denied, core.transfer(b, a.uuid(), 999, "metadata test",
                                              denied.requestId, denied), Status::InsufficientFunds);
                auto rejected = metadata("metadata-runtime-denied", 1, 1, 1, a.playerId());
                save(owner, rejected,
                     core.recordRejectedRequest(owner, a.uuid(), "grant",
                                                rejected.canonicalRequestPayload, "metadata test",
                                                rejected.requestId, Status::PermissionDenied,
                                                rejected), Status::PermissionDenied);
                Db db(path);
                for (const auto &item : stored) {
                    const auto receipt = sqlQuote(item.receipt);
                    require(db.query("SELECT json_type(" + receipt + ",'$.request')") == "object" &&
                                db.query("SELECT json_array_length(" + receipt + ",'$.accounts')") == "4" &&
                                db.query("SELECT json_array_length(" + receipt + ",'$.players')") == "2",
                            "Receipt snapshots missing");
                    require(db.query("SELECT json_extract(" + receipt + ",'$.createdAt')>0 AND "
                                     "json_extract(" + receipt + ",'$.completedAt')>=json_extract(" +
                                     receipt + ",'$.createdAt')") == "1",
                            "Receipt completion time missing");
                    require(db.query("SELECT json_extract(" + receipt + ",'$.auditRevision')>0 AND "
                                     "json_extract(" + receipt + ",'$.ledgerRevision')>0") == "1",
                            "Receipt durable revisions missing");
                }
                require(db.query("SELECT json_extract(" + sqlQuote(stored[0].receipt) +
                                 ",'$.accounts[2].revision')") == "2" &&
                            db.query("SELECT json_extract(" + sqlQuote(stored[2].receipt) +
                                     ",'$.accounts[0].balance')") == "95" &&
                            db.query("SELECT json_extract(" + sqlQuote(stored[3].receipt) +
                                     ",'$.players[1].permissionRevision')") == "2",
                        "Snapshots do not contain transaction-time values");
                equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 20, "later reward", "later")
                          .status, Status::Ok);
            }
            Core core(path.string(), "1000");
            for (const auto &item : stored) {
                const auto restored = *core.restoreActor(item.actorUuid).actor;
                DomainRequestMetadata meta{"market", "test", item.key, 1};
                meta.canonicalRequestPayload = item.request;
                const auto polled = core.receipt(restored, item.key, meta);
                equal(polled.status, item.status);
                require(polled.replayed && polled.receipt == item.receipt,
                        "Restart poll changed request, timestamps or snapshots");
            }
        });
        test("receipt polling by transaction ID is durable and actor-module scoped", [&] {
            const auto path = root / "poll.sqlite";
            std::string txId, receipt;
            const DomainRequestMetadata metadata{"market", "money.transfer",
                                                 "original-poll-request", 1};
            {
                Core core(path.string(), "1000");
                const auto a = actor(core, aliceIdentity), b = actor(core, bobIdentity);
                const auto result =
                    core.transfer(a, b.uuid(), 1, "unfunded payment", "poll-key", metadata);
                equal(result.status, Status::InsufficientFunds);
                receipt = result.receipt;
                const std::string marker = "\"transactionId\":\"";
                const auto begin = receipt.find(marker) + marker.size();
                txId = receipt.substr(begin, receipt.find('"', begin) - begin);
            }
            Core core(path.string(), "1000");
            const auto a = *core.restoreActor(aliceIdentity.uuid).actor,
                       b = *core.restoreActor(bobIdentity.uuid).actor;
            const auto result = core.receiptById(a, txId, metadata);
            equal(result.status, Status::InsufficientFunds);
            require(result.receipt == receipt && result.replayed,
                    "Durable poll lost denied receipt");
            equal(core.receiptById(b, txId, metadata).status, Status::NotFound);
            auto other = metadata;
            other.moduleId = "quests";
            equal(core.receiptById(a, txId, other).status, Status::NotFound);
        });
        test("per-consumer outbox dedup survives restart and does not cross-ack", [&] {
            const auto path = root / "outbox.sqlite";
            std::int64_t id{};
            {
                Core core(path.string(), "1000");
                const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
                equal(core.registerConsumer("hud"), Status::Ok);
                equal(core.registerConsumer("announcements"), Status::Ok);
                equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward", "reward").status,
                      Status::Ok);
                const auto events = core.outboxFor("hud");
                require(events.size() == 1, "New event not scheduled");
                id = events[0].id;
                equal(core.acknowledgeOutbox("hud", id), Status::Ok);
                equal(core.acknowledgeOutbox("hud", id), Status::Ok);
                require(core.outboxFor("hud").empty() &&
                            core.outboxFor("announcements").size() == 1,
                        "Consumer ack crossed scope");
            }
            Core core(path.string(), "1000");
            equal(core.registerConsumer("hud"), Status::Ok);
            require(core.outboxFor("hud").empty(), "Acknowledged event replayed after restart");
            equal(core.acknowledgeOutbox("announcements", id), Status::Ok);
            const auto owner = *core.restoreActor(ownerIdentity.uuid).actor;
            require(core.ownerGrant(owner, aliceIdentity.uuid, Asset::Coin, 10, "reward", "reward")
                        .replayed,
                    "Transaction replay failed");
            require(core.outboxFor("announcements").empty(),
                    "Transaction replay recreated delivery");
        });
        test("offline outbox retry retains attempts and retries only when due", [&] {
            const auto path = root / "offline.sqlite";
            std::int64_t id{};
            {
                Core core(path.string(), "1000");
                const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
                equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "reward", "reward").status,
                      Status::Ok);
                equal(core.registerConsumer("hud"), Status::Ok);
                id = core.outboxFor("hud")[0].id;
                equal(core.recordOutboxAttempt("hud", id, "Player offline", 60000, true),
                      Status::Ok);
                require(core.outboxFor("hud").empty(), "Offline retry ignored delay");
            }
            Core core(path.string(), "1000");
            require(core.outboxFor("hud").empty(), "Retry delay lost at restart");
            equal(core.recordOutboxAttempt("hud", id, "Player offline", 0, true), Status::Ok);
            const auto events = core.outboxFor("hud");
            require(events.size() == 1 && events[0].retryCount == 2 &&
                        events[0].deliveryStatus == "offline",
                    "Offline metadata lost");
            equal(core.acknowledgeOutbox("hud", id), Status::Ok);
            equal(core.recordOutboxAttempt("hud", id, "Late callback", 0), Status::Ok);
            require(core.outboxFor("hud").empty(), "Late retry resurrected acknowledged delivery");
        });
        test("legacy acknowledgement does not erase another consumer's pending receipt", [&] {
            const auto path = root / "mixed-outbox.sqlite";
            Core core(path.string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            equal(core.registerConsumer("hud"), Status::Ok);
            equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 10, "grant", "grant").status,
                  Status::Ok);
            const auto id = core.outboxFor("hud")[0].id;
            equal(core.acknowledgeOutbox(id), Status::Ok);
            Db db(path);
            require(db.query("SELECT count(*) FROM pending_receipt") == "1",
                    "Legacy ack prematurely cleared consumer projection");
            equal(core.acknowledgeOutbox("hud", id), Status::Ok);
            require(db.query("SELECT count(*) FROM pending_receipt") == "0" &&
                        core.pendingOutbox().empty(),
                    "Completed projection remained pending");
        });
        test("history cursor orders retained events including acknowledged rows and validates "
             "bounds",
             [&] {
                 Core core((root / "outbox-history.sqlite").string(), "1000");
                 const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
                 equal(core.registerConsumer("hud"), Status::Ok);
                 for (int i = 0; i < 3; ++i)
                     equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 1, "grant",
                                           "grant-" + std::to_string(i))
                               .status,
                           Status::Ok);
                 const auto events = core.eventsAfter(0);
                 require(events.size() == 3 && events[0].id < events[1].id &&
                             events[1].id < events[2].id,
                         "History order wrong");
                 equal(core.acknowledgeOutbox("hud", events[0].id), Status::Ok);
                 const auto retained = core.eventsAfter(0, 1);
                 require(retained.size() == 1 && retained[0].id == events[0].id &&
                             retained[0].deliveryStatus == "acknowledged",
                         "Ack removed cursor history");
                 const auto after = core.eventsAfter(events[0].id);
                 require(after.size() == 2 && after[0].id == events[1].id &&
                             after[1].id == events[2].id,
                         "Exclusive cursor filtering wrong");
                 require(core.eventsAfter(std::numeric_limits<std::int64_t>::max()).empty(),
                         "End cursor not empty");
                 for (const auto &[cursor, limit] :
                      std::vector<std::pair<std::int64_t, std::size_t>>{
                          {-1, 100}, {0, 0}, {0, 1001}}) {
                     bool rejected = false;
                     try {
                         core.eventsAfter(cursor, limit);
                     } catch (const std::invalid_argument &) {
                         rejected = true;
                     }
                     require(rejected, "Invalid history cursor or limit accepted");
                 }
             });
        test("history event time and request attribution remain exact after restart", [&] {
            const auto path = root / "outbox-history-attribution.sqlite";
            std::vector<OutboxEvent> original;
            {
                Core core(path.string(), "1000");
                const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
                equal(core.registerConsumer("hud"), Status::Ok);
                const DomainRequestMetadata grant{"market", "reward", "history-grant", 1},
                    role{"staff", "role.manage", "history-role", 1};
                equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 1, "grant", "grant", grant)
                          .status, Status::Ok);
                equal(core.setRole(owner, a.uuid(), Role::Build, true, "role", "role", role)
                          .status, Status::Ok);
                original = core.eventsAfter(0);
                require(original.size() == 2 && original[0].requestId == grant.requestId &&
                            original[1].requestId == role.requestId,
                        "History event request attribution missing");
                Db db(path);
                for (const auto &event : original) {
                    require(event.createdAt > 0 &&
                                db.query("SELECT createdAt FROM outbox WHERE id=" +
                                         std::to_string(event.id)) == std::to_string(event.createdAt),
                            "History event time is not the persisted value");
                }
                const auto pending = core.pendingOutbox(), deliveries = core.outboxFor("hud");
                require(pending.size() == 2 && deliveries.size() == 2 &&
                            pending[0].createdAt == 0 && pending[0].requestId.empty() &&
                            deliveries[0].createdAt == 0 && deliveries[0].requestId.empty(),
                        "Legacy delivery queries changed metadata semantics");
                equal(core.acknowledgeOutbox(original[0].id), Status::Ok);
            }
            Core core(path.string(), "1000");
            const auto history = core.eventsAfter(0);
            require(history.size() == original.size() &&
                        history[0].deliveryStatus == "acknowledged",
                    "Acknowledged history disappeared after restart");
            for (std::size_t i = 0; i < history.size(); ++i)
                require(history[i].createdAt == original[i].createdAt &&
                            history[i].requestId == original[i].requestId,
                        "Restart changed original event metadata");
        });
        test("account and ledger revisions increase only with committed wallet entries", [&] {
            Core core((root / "wallet-revision.sqlite").string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            const auto before = core.balance(a.uuid());
            const auto r = core.ownerGrant(owner, a.uuid(), Asset::Coin, 5, "reward", "reward");
            equal(r.status, Status::Ok);
            const auto after = core.balance(a.uuid());
            require(after.revision == before.revision + 1 &&
                        after.ledgerRevision > before.ledgerRevision,
                    "Wallet revisions missing");
            require(core.ownerGrant(owner, a.uuid(), Asset::Coin, 5, "reward", "reward").replayed,
                    "Replay failed");
            equal(core.adjustAsset(owner, a.uuid(), Asset::Coin, -6, "overdraft", "denied").status,
                  Status::InsufficientFunds);
            const auto final = core.balance(a.uuid());
            require(final.revision == after.revision &&
                        final.ledgerRevision == after.ledgerRevision,
                    "Replay or denial changed revisions");
        });
        test("001 upgrade preserves identities roles ledger receipts and idempotent replay", [&] {
            const auto path = root / "upgrade.sqlite";
            makeV1(path);
            Core core(path.string(), "1000");
            const auto owner = *core.restoreActor(ownerIdentity.uuid).actor,
                       a = *core.restoreActor(aliceIdentity.uuid).actor;
            require(balance(core, a) == 100 && core.hasRole(a, Role::Build),
                    "Upgrade lost old data");
            const auto replay =
                core.ownerGrant(owner, a.uuid(), Asset::Coin, 100, "old grant", "old-key");
            require(replay.replayed && replay.receipt.find("tx_old") != std::string::npos,
                    "Old request cannot replay");
            require(core.balance(a.uuid()).revision == 1 && core.selfcheck().value.ledgerConsistent,
                    "Upgrade ledger or revision lost");
            const auto copy = backups(path);
            require(copy.size() == 1 && fs::exists(copy[0].string() + ".sha256"),
                    "Upgrade backup missing");
            Db db(copy[0]);
            require(db.query("PRAGMA user_version") == "1", "Backup is not pre-upgrade snapshot");
        });
        test("failed 002 migration rolls back schema and data; retry succeeds", [&] {
            const auto path = root / "upgrade-failure.sqlite";
            makeV1(path);
            bool failed = false;
            try {
                Core core(path.string(), "1000", [](FaultPoint p) {
                    if (p == FaultPoint::DuringMigration)
                        throw InjectedFault();
                });
            } catch (const InjectedFault &) {
                failed = true;
            }
            require(failed, "Migration injection did not execute");
            {
                Db db(path);
                require(db.query("PRAGMA user_version") == "1" &&
                            db.query("SELECT count(*) FROM pragma_table_info('players')") == "2",
                        "Failed migration committed schema");
                require(db.query("SELECT balance FROM accounts WHERE "
                                 "uuid='bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb' AND asset='coin'") ==
                            "100",
                        "Failed migration lost balance");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            Core core(path.string(), "1000");
            require(core.selfcheck().value.ledgerConsistent, "Migration retry broken");
        });
        test("fresh migration failure leaves no partial tables and fresh success has no backup",
             [&] {
                 const auto path = root / "fresh-failure.sqlite";
                 bool failed = false;
                 try {
                     Core core(path.string(), "1000", [](FaultPoint p) {
                         if (p == FaultPoint::DuringMigration)
                             throw InjectedFault();
                     });
                 } catch (const InjectedFault &) {
                     failed = true;
                 }
                 require(failed && backups(path).empty(), "Fresh DB made an empty backup");
                 {
                     Db db(path);
                     require(
                         db.query("PRAGMA user_version") == "0" &&
                             db.query("SELECT count(*) FROM sqlite_master WHERE type='table'") ==
                                 "0",
                         "Partial fresh schema committed");
                 }
                 Core core((root / "fresh-success.sqlite").string(), "1000");
                 require(backups(root / "fresh-success.sqlite").empty(),
                         "Fresh success backed up empty DB");
             });
        test("newer schema and mismatched migration checksum are safely refused", [&] {
            const auto path = root / "newer.sqlite";
            {
                Db db(path, true);
                db.exec("PRAGMA user_version=999");
            }
            const auto before = read(path);
            bool rejected = false;
            try {
                Core core(path.string(), "1000");
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            require(rejected && read(path) == before, "Newer DB changed or accepted");
            const auto bad = root / "checksum.sqlite";
            {
                Core core(bad.string(), "1000");
            }
            {
                Db db(bad, true);
                db.exec("UPDATE schema_migration SET checksum='bad' WHERE version=2");
            }
            rejected = false;
            try {
                Core core(bad.string(), "1000");
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            require(rejected, "Changed migration checksum accepted");
        });
        test("real child process loss during signed asset update gives exactly-once recovery", [&] {
            for (const auto point :
                 {FaultPoint::AfterDebit, FaultPoint::BeforeCommit, FaultPoint::AfterCommit}) {
                const auto path =
                    root / ("asset-crash-" + std::to_string(static_cast<int>(point)) + ".sqlite");
                {
                    Core core(path.string(), "1000");
                    const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
                    equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 100, "seed", "seed").status,
                          Status::Ok);
                }
                auto command = commandQuote(fs::absolute(argv[0]).string()) + " --crash " +
                               commandQuote(path.string()) + " " +
                               std::to_string(static_cast<int>(point));
#ifdef _WIN32
                command = '"' + command + '"';
                require(std::system(command.c_str()) == 87,
                        "Child did not reach injected process loss");
#else
                require(std::system(command.c_str()) == (87 << 8), "Child did not reach injected process loss");
#endif
                Core core(path.string(), "1000");
                const auto owner = *core.restoreActor(ownerIdentity.uuid).actor,
                           a = *core.restoreActor(aliceIdentity.uuid).actor;
                require(balance(core, a) == (point == FaultPoint::AfterCommit ? 75 : 100),
                        "Partial asset change visible");
                require(core.balance(a.uuid()).revision ==
                            (point == FaultPoint::AfterCommit ? 2U : 1U),
                        "Partial wallet revision committed");
                const auto result = core.adjustAsset(owner, a.uuid(), Asset::Coin, -25,
                                                     "crash adjustment", "crash-adjust");
                equal(result.status, Status::Ok);
                require(result.replayed == (point == FaultPoint::AfterCommit) &&
                            balance(core, a) == 75,
                        "Crash recovery was not exactly once");
                require(core.balance(a.uuid()).revision == 2, "Crash recovery duplicated revision");
                require(core.selfcheck().value.ledgerConsistent, "Crash damaged ledger");
            }
        });
        test("selfcheck identifies authoritative wallet divergence without a DB handle", [&] {
            const auto path = root / "diagnostic.sqlite";
            Core core(path.string(), "1000");
            const auto owner = actor(core, ownerIdentity), a = actor(core, aliceIdentity);
            equal(core.ownerGrant(owner, a.uuid(), Asset::Coin, 1, "seed", "seed").status,
                  Status::Ok);
            auto good = core.selfcheck();
            equal(good.status, Status::Ok);
            require(good.value.players == 2 && good.value.transactions == 1 &&
                        good.value.ledgerEntries == 1 && good.value.ledgerConsistent &&
                        good.value.receiptsConsistent,
                    "Diagnostic counters wrong");
            {
                Db db(path, true);
                db.exec("UPDATE accounts SET balance=2 WHERE "
                        "uuid='bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb' AND asset='coin'");
            }
            require(!core.selfcheck().value.ledgerConsistent,
                    "Corrupt wallet escaped reconciliation");
        });
        std::cout << "PASS " << groups
                  << " Phase 2 domain groups; live players NOT RUN in domain suite\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
