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
#include <string>
#include <thread>
#include <type_traits>

using namespace eternal::core;
namespace fs = std::filesystem;
namespace {
const std::string ownerXuid = "1000"; // Synthetic identity, never an operator's account.
const VerifiedIdentity ownerIdentity{"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", ownerXuid};
const VerifiedIdentity aliceIdentity{"bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", "1001"};
const VerifiedIdentity bobIdentity{"cccccccc-cccc-cccc-cccc-cccccccccccc", "1002"};
int groups = 0;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void equal(Status actual, Status expected) {
    require(actual == expected,
            std::string("Expected ") + name(expected) + ", got " + name(actual));
}
template <class F> void test(const std::string &name, F fn) {
    fn();
    ++groups;
    std::cout << "PASS " << name << '\n';
}
Actor actor(Core &core, const VerifiedIdentity &identity) {
    auto value = core.ensurePlayer(identity);
    equal(value.status, Status::Ok);
    require(value.actor.has_value(), "Missing verified actor");
    return *value.actor;
}
std::int64_t cash(Core &core, const Actor &user, Asset asset = Asset::Coin) {
    auto result = core.balance(user.uuid(), asset);
    equal(result.status, Status::Ok);
    return result.amount;
}
struct ReadDb {
    sqlite3 *db{};
    explicit ReadDb(const fs::path &path, bool writable = false) {
        if (sqlite3_open_v2(path.string().c_str(), &db,
                            writable ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                     : SQLITE_OPEN_READONLY,
                            nullptr) != SQLITE_OK)
            throw std::runtime_error("Test SQLite open failed");
    }
    ~ReadDb() { sqlite3_close(db); }
    void exec(const char *sql) {
        if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    std::string query(const char *sql) {
        sqlite3_stmt *statement{};
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        const int rc = sqlite3_step(statement);
        std::string result;
        if (rc == SQLITE_ROW) {
            const auto *p = sqlite3_column_text(statement, 0);
            if (p)
                result = reinterpret_cast<const char *>(p);
        } else if (rc != SQLITE_DONE) {
            sqlite3_finalize(statement);
            throw std::runtime_error(sqlite3_errmsg(db));
        }
        sqlite3_finalize(statement);
        return result;
    }
};
std::string read(const fs::path &file) {
    std::ifstream input(file, std::ios::binary);
    require(bool(input), "Cannot read test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<fs::path> migrationBackups(const fs::path &file) {
    std::vector<fs::path> matches;
    const auto prefix = file.filename().string() + ".pre-migration-001-";
    for (const auto &item : fs::directory_iterator(file.parent_path()))
        if (item.path().filename().string().find(prefix) == 0 &&
            item.path().extension() == ".sqlite")
            matches.push_back(item.path());
    return matches;
}
struct InjectedFault : std::runtime_error {
    InjectedFault() : runtime_error("Injected exception, not a process crash") {};
};
void seed(const fs::path &file) {
    Core core(file.string(), ownerXuid);
    const auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity);
    actor(core, bobIdentity);
    equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 10000, "initial grant", "seed").status,
          Status::Ok);
}
int crashChild(const std::string &db, int point) {
    Core core(db, ownerXuid, [point](FaultPoint at) {
        if (static_cast<int>(at) == point)
            std::_Exit(86);
    });
    const auto alice = actor(core, aliceIdentity);
    core.transfer(alice, bobIdentity.uuid, 125, "crash test", "crash-key");
    return 2;
}
std::string commandQuote(const std::string &input) {
    require(input.find_first_of("\"\r\n&|<>") == std::string::npos, "Unsafe test subprocess path");
    return "\"" + input + "\"";
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--crash")
        return crashChild(argv[2], std::stoi(argv[3]));
    try {
        static_assert(!std::is_constructible_v<Actor, std::string, std::string>);
        const auto suffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path root = fs::temp_directory_path() / ("EternalCoreNativeTests-" + suffix);
        fs::create_directories(root);
        std::cout << "TEST_DIRECTORY " << root.string() << '\n';
        test("SHA256 vectors and numbered migration SQL equals embedded schema", [&] {
            require(detail::sha256("") ==
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "Empty SHA256 mismatch");
            require(detail::sha256("abc") ==
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                    "SHA256 abc mismatch");
            const auto sql = fs::path(__FILE__).parent_path().parent_path().parent_path() /
                             "migrations/EternalCore/001_initial.sql";
            require(read(sql) == detail::schemaV1, "SQL and compiled schema differ");
        });
        test("unique verified UUID/XUID and owner rights, never source-name authorization", [&] {
            Core core((root / "identity.sqlite").string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                 bob = actor(core, bobIdentity);
            require(core.hasRole(owner, Role::Operations), "Owner lacks rights");
            require(!core.hasRole(alice, Role::Operations), "Player has unsolicited role");
            equal(core.ensurePlayer({alice.uuid(), "1003"}).status, Status::Conflict);
            equal(core.ensurePlayer({bob.uuid(), alice.xuid()}).status, Status::Conflict);
            equal(core.ensurePlayer({"invalid", "1004"}).status, Status::Invalid);
            auto uppercase = core.ensurePlayer({"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA", ownerXuid});
            equal(uppercase.status, Status::Ok);
            require(!uppercase.created, "Canonical UUID duplicated");
            equal(core.ownerGrant(alice, bob.uuid(), Asset::Coin, 10,
                                  "owner SYSTEM authorized source", "fake-source")
                      .status,
                  Status::PermissionDenied);
            equal(
                core.setRole(alice, bob.uuid(), Role::Operations, true, "claim owner", "fake-role")
                    .status,
                Status::PermissionDenied);
            equal(
                core.setRole(owner, alice.uuid(), Role::Operations, true, "appointment", "role-add")
                    .status,
                Status::Ok);
            require(core.hasRole(alice, Role::Operations), "Role was not set");
            equal(core.ownerGrant(alice, bob.uuid(), Asset::Coin, 10,
                                  "Operations are not mint rights", "op-grant")
                      .status,
                  Status::PermissionDenied);
            equal(core.setRole(owner, alice.uuid(), Role::Operations, false, "revocation",
                               "role-remove")
                      .status,
                  Status::Ok);
            require(!core.hasRole(alice, Role::Operations), "Role was not revoked");
        });
        test("coin cents and reputation are integer authoritative assets; offline transfer", [&] {
            Core core((root / "accounts.sqlite").string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                 bob = actor(core, bobIdentity);
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 12345, "cents", "coins").status,
                  Status::Ok);
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Reputation, 50, "reputation", "rep")
                      .status,
                  Status::Ok);
            equal(core.transfer(alice, bob.uuid(), 125, "offline target", "offline").status,
                  Status::Ok);
            require(cash(core, alice) == 12220 && cash(core, bob) == 125 &&
                        cash(core, alice, Asset::Reputation) == 50,
                    "Integer asset mismatch");
            equal(core.transfer(alice, bob.uuid(), 0, "zero", "invalid-zero").status,
                  Status::Invalid);
            equal(core.transfer(alice, bob.uuid(), -1, "negative", "invalid-negative").status,
                  Status::Invalid);
            equal(core.transfer(alice, bob.uuid(), 20000, "insufficient", "insufficient").status,
                  Status::InsufficientFunds);
            equal(core.transfer(alice, "dddddddd-dddd-dddd-dddd-dddddddddddd", 1, "missing",
                                "missing")
                      .status,
                  Status::NotFound);
            equal(core.transfer(alice, alice.uuid(), 1, "self", "self").status, Status::Invalid);
            equal(core.ownerGrant(owner, bob.uuid(), Asset::Coin, 1, " ", "blank-reason").status,
                  Status::Invalid);
        });
        test("same key replays exact receipt; changed canonical payload conflicts", [&] {
            const auto file = root / "idempotency.sqlite";
            Core core(file.string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                 bob = actor(core, bobIdentity);
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 10000, "grant",
                                  "idempotent-grant")
                      .status,
                  Status::Ok);
            const auto result = core.transfer(alice, bob.uuid(), 200, "gift", "stable-key");
            equal(result.status, Status::Ok);
            const auto count = core.pendingOutbox().size();
            for (int i = 0; i < 5; i++) {
                const auto replay = core.transfer(alice, bob.uuid(), 200, "gift", "stable-key");
                equal(replay.status, Status::Ok);
                require(replay.replayed && replay.receipt == result.receipt,
                        "Receipt replay changed");
            }
            equal(core.transfer(alice, bob.uuid(), 201, "gift", "stable-key").status,
                  Status::Conflict);
            equal(core.transfer(alice, bob.uuid(), 200, "different reason", "stable-key").status,
                  Status::Conflict);
            equal(core.ownerGrant(owner, bob.uuid(), Asset::Coin, 200, "gift", "idempotent-grant")
                      .status,
                  Status::Conflict);
            equal(core.setRole(owner, bob.uuid(), Role::Operations, true, "different operation",
                               "idempotent-grant")
                      .status,
                  Status::Conflict);
            require(cash(core, alice) == 9800 && cash(core, bob) == 200 &&
                        core.pendingOutbox().size() == count,
                    "Replay duplicated assets or outbox");
            ReadDb db(file);
            require(
                db.query("SELECT retryCount FROM transactions WHERE idempotencyKey='stable-key'") ==
                    "5",
                "Retry count mismatch");
            require(db.query("SELECT count(*) FROM entries") == "3", "Duplicate ledger entry");
            require(db.query("SELECT count(*) FROM audit") == "2", "Duplicate committed audit");
        });
        test("trusted actors independently use the same key and replay only their own receipt",
             [&] {
                 const auto file = root / "actor-scopes.sqlite";
                 Result grant, aliceTransfer, bobTransfer;
                 {
                     Core core(file.string(), ownerXuid);
                     const auto owner = actor(core, ownerIdentity),
                                alice = actor(core, aliceIdentity), bob = actor(core, bobIdentity);
                     grant = core.ownerGrant(owner, alice.uuid(), Asset::Coin, 1000, "seed Alice",
                                             "shared-key");
                     equal(grant.status, Status::Ok);
                     equal(core.ownerGrant(owner, bob.uuid(), Asset::Coin, 1000, "seed Bob",
                                           "bob-seed")
                               .status,
                           Status::Ok);
                     aliceTransfer =
                         core.transfer(alice, bob.uuid(), 100, "gift to Bob", "shared-key");
                     bobTransfer =
                         core.transfer(bob, alice.uuid(), 50, "gift to Alice", "shared-key");
                     equal(aliceTransfer.status, Status::Ok);
                     equal(bobTransfer.status, Status::Ok);
                     require(!aliceTransfer.replayed && !bobTransfer.replayed &&
                                 grant.receipt != aliceTransfer.receipt &&
                                 aliceTransfer.receipt != bobTransfer.receipt,
                             "Actors collided or received another actor's receipt");
                 }
                 Core restarted(file.string(), ownerXuid);
                 const auto owner = actor(restarted, ownerIdentity),
                            alice = actor(restarted, aliceIdentity),
                            bob = actor(restarted, bobIdentity);
                 const auto ownerReplay = restarted.ownerGrant(owner, alice.uuid(), Asset::Coin,
                                                               1000, "seed Alice", "shared-key");
                 const auto aliceReplay =
                     restarted.transfer(alice, bob.uuid(), 100, "gift to Bob", "shared-key");
                 const auto bobReplay =
                     restarted.transfer(bob, alice.uuid(), 50, "gift to Alice", "shared-key");
                 require(ownerReplay.replayed && ownerReplay.receipt == grant.receipt &&
                             aliceReplay.replayed && aliceReplay.receipt == aliceTransfer.receipt &&
                             bobReplay.replayed && bobReplay.receipt == bobTransfer.receipt,
                         "Scoped receipt replay failed after restart");
                 equal(
                     restarted.transfer(alice, bob.uuid(), 101, "gift to Bob", "shared-key").status,
                     Status::Conflict);
                 equal(restarted.transfer(bob, alice.uuid(), 50, "changed", "shared-key").status,
                       Status::Conflict);
                 require(cash(restarted, alice) == 950 && cash(restarted, bob) == 1050,
                         "Scoped replay duplicated assets");
                 ReadDb db(file);
                 require(
                     db.query("SELECT count(*) FROM transactions WHERE "
                              "executorScope='core.domain.v1' AND idempotencyKey='shared-key'") ==
                         "3",
                     "Executor and actor scoped transactions missing");
                 require(db.query("SELECT count(*) FROM receipts") == "4",
                         "Scoped receipt count mismatch");
             });
        test("XUID is bounded uint64 and request text must be canonical valid UTF-8", [&] {
            const auto file = root / "input-boundaries.sqlite";
            Core core(file.string(), ownerXuid);
            const auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                       bob = actor(core, bobIdentity);
            equal(
                core.ensurePlayer({"dddddddd-dddd-dddd-dddd-dddddddddddd", "18446744073709551615"})
                    .status,
                Status::Ok);
            equal(
                core.ensurePlayer({"eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee", "18446744073709551616"})
                    .status,
                Status::Invalid);
            equal(
                core.ensurePlayer({"eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee", "99999999999999999999"})
                    .status,
                Status::Invalid);
            bool invalidOwnerRejected = false;
            try {
                Core invalid((root / "invalid-owner-range.sqlite").string(),
                             "18446744073709551616");
            } catch (const std::invalid_argument &) {
                invalidOwnerRejected = true;
            }
            require(invalidOwnerRejected, "Out-of-range owner XUID accepted");
            const std::vector<std::string> invalidUtf8{std::string("\xc0\xaf", 2),
                                                       std::string("\xe0\x80\xaf", 3),
                                                       std::string("\xf0\x80\x80\xaf", 4),
                                                       std::string("\xed\xa0\x80", 3),
                                                       std::string("\xf4\x90\x80\x80", 4),
                                                       std::string("\x80", 1),
                                                       std::string("\xff", 1),
                                                       std::string("\xe2\x82", 2)};
            for (const auto &invalid : invalidUtf8) {
                equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 100, invalid,
                                      "invalid-reason")
                          .status,
                      Status::Invalid);
                equal(
                    core.ownerGrant(owner, alice.uuid(), Asset::Coin, 100, "valid reason", invalid)
                        .status,
                    Status::Invalid);
                equal(
                    core.transfer(alice, bob.uuid(), 1, invalid, "invalid-transfer-reason").status,
                    Status::Invalid);
                equal(core.transfer(alice, bob.uuid(), 1, "valid reason", invalid).status,
                      Status::Invalid);
                equal(core.setRole(owner, bob.uuid(), Role::Content, true, invalid,
                                   "invalid-role-reason")
                          .status,
                      Status::Invalid);
                equal(core.setRole(owner, bob.uuid(), Role::Content, true, "valid reason", invalid)
                          .status,
                      Status::Invalid);
            }
            const std::string chinese = "\xe6\xb1\x9f\xe6\xb9\x96";
            const std::string supplementary = "\xf0\x9f\x8c\x8f";
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 100, chinese,
                                  chinese + supplementary)
                      .status,
                  Status::Ok);
            ReadDb db(file);
            require(db.query("SELECT count(*) FROM transactions") == "1",
                    "Invalid text created committed requests");
        });
        test("signed integer overflow refuses grant and transfer atomically", [&] {
            Core core((root / "overflow.sqlite").string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                 bob = actor(core, bobIdentity);
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 10, "seed", "small").status,
                  Status::Ok);
            equal(core.ownerGrant(owner, bob.uuid(), Asset::Coin,
                                  std::numeric_limits<std::int64_t>::max(), "max", "maximum")
                      .status,
                  Status::Ok);
            equal(core.ownerGrant(owner, bob.uuid(), Asset::Coin, 1, "overflow", "overflow-grant")
                      .status,
                  Status::Overflow);
            equal(core.transfer(alice, bob.uuid(), 1, "overflow", "overflow-transfer").status,
                  Status::Overflow);
            require(cash(core, alice) == 10 &&
                        cash(core, bob) == std::numeric_limits<std::int64_t>::max(),
                    "Overflow modified balances");
        });
        test("busy writer refuses then safely accepts same key after lock release", [&] {
            const auto file = root / "busy.sqlite";
            Core core(file.string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity),
                 bob = actor(core, bobIdentity);
            equal(
                core.ownerGrant(owner, alice.uuid(), Asset::Coin, 100, "seed", "busy-seed").status,
                Status::Ok);
            {
                ReadDb db(file, true);
                db.exec("BEGIN IMMEDIATE");
                equal(core.transfer(alice, bob.uuid(), 10, "busy", "busy-key").status,
                      Status::Busy);
                db.exec("ROLLBACK");
            }
            equal(core.transfer(alice, bob.uuid(), 10, "busy", "busy-key").status, Status::Ok);
            require(cash(core, alice) == 90 && cash(core, bob) == 10, "Busy retry mismatch");
        });
        test("concurrent connections with one key commit only one transfer", [&] {
            const auto file = root / "concurrent.sqlite";
            seed(file);
            Core first(file.string(), ownerXuid), second(file.string(), ownerXuid);
            const auto aliceA = actor(first, aliceIdentity), aliceB = actor(second, aliceIdentity);
            Result resultA, resultB;
            std::thread a([&] {
                resultA = first.transfer(aliceA, bobIdentity.uuid, 100, "concurrent", "same-key");
            });
            std::thread b([&] {
                resultB = second.transfer(aliceB, bobIdentity.uuid, 100, "concurrent", "same-key");
            });
            a.join();
            b.join();
            if (resultA.status == Status::Busy)
                resultA = first.transfer(aliceA, bobIdentity.uuid, 100, "concurrent", "same-key");
            if (resultB.status == Status::Busy)
                resultB = second.transfer(aliceB, bobIdentity.uuid, 100, "concurrent", "same-key");
            equal(resultA.status, Status::Ok);
            equal(resultB.status, Status::Ok);
            require(resultA.receipt == resultB.receipt && resultA.replayed != resultB.replayed,
                    "Concurrent request committed twice");
            require(cash(first, aliceA) == 9900 && first.balance(bobIdentity.uuid).amount == 100,
                    "Concurrent balances are inconsistent");
            ReadDb db(file);
            require(db.query("SELECT count(*) FROM transactions WHERE idempotencyKey='same-key'") ==
                        "1",
                    "Concurrent duplicate transaction");
        });
        test("exception fault injection rolls back before commit and replays after commit", [&] {
            for (auto point :
                 {FaultPoint::AfterDebit, FaultPoint::BeforeCommit, FaultPoint::AfterCommit}) {
                const auto file =
                    root / ("exception-" + std::to_string(static_cast<int>(point)) + ".sqlite");
                seed(file);
                bool thrown = false;
                {
                    Core core(file.string(), ownerXuid, [point](FaultPoint at) {
                        if (at == point)
                            throw InjectedFault{};
                    });
                    auto alice = actor(core, aliceIdentity);
                    try {
                        core.transfer(alice, bobIdentity.uuid, 125, "exception fault", "fault-key");
                    } catch (const InjectedFault &) {
                        thrown = true;
                    }
                }
                require(thrown, "Injected exception did not fire");
                Core restarted(file.string(), ownerXuid);
                auto alice = actor(restarted, aliceIdentity), bob = actor(restarted, bobIdentity);
                const bool committed = point == FaultPoint::AfterCommit;
                require(cash(restarted, alice) == (committed ? 9875 : 10000) &&
                            cash(restarted, bob) == (committed ? 125 : 0),
                        "Exception atomicity failed");
                auto result =
                    restarted.transfer(alice, bob.uuid(), 125, "exception fault", "fault-key");
                equal(result.status, Status::Ok);
                require(result.replayed == committed, "Wrong exception replay state");
                const auto replay =
                    restarted.transfer(alice, bob.uuid(), 125, "exception fault", "fault-key");
                require(replay.replayed && replay.receipt == result.receipt &&
                            cash(restarted, bob) == 125,
                        "Exception replay duplicated assets");
            }
        });
        test("real child _Exit simulates process loss after debit, precommit and postcommit", [&] {
            const auto executable = fs::absolute(argv[0]).string();
            for (auto point :
                 {FaultPoint::AfterDebit, FaultPoint::BeforeCommit, FaultPoint::AfterCommit}) {
                const auto file =
                    root / ("process-crash-" + std::to_string(static_cast<int>(point)) + ".sqlite");
                seed(file);
                auto command = commandQuote(executable) + " --crash " +
                               commandQuote(file.string()) + " " +
                               std::to_string(static_cast<int>(point));
#ifdef _WIN32
                command = "\"" + command + "\"";
#endif
                require(std::system(command.c_str()) != 0, "Child did not terminate abnormally");
                Core restarted(file.string(), ownerXuid);
                auto alice = actor(restarted, aliceIdentity), bob = actor(restarted, bobIdentity);
                const bool committed = point == FaultPoint::AfterCommit;
                require(cash(restarted, alice) == (committed ? 9875 : 10000) &&
                            cash(restarted, bob) == (committed ? 125 : 0),
                        "Process loss atomicity failed");
                const auto result =
                    restarted.transfer(alice, bob.uuid(), 125, "crash test", "crash-key");
                equal(result.status, Status::Ok);
                require(result.replayed == committed, "Wrong process loss replay state");
                const auto replay =
                    restarted.transfer(alice, bob.uuid(), 125, "crash test", "crash-key");
                require(replay.replayed && replay.receipt == result.receipt &&
                            cash(restarted, bob) == 125,
                        "Crash replay duplicated assets");
                ReadDb db(file);
                require(db.query("PRAGMA integrity_check") == "ok" &&
                            db.query("PRAGMA foreign_key_check").empty(),
                        "Recovery integrity failed");
            }
        });
        test("durable outbox acknowledgements clear pending projection, not receipts", [&] {
            const auto file = root / "outbox.sqlite";
            Core core(file.string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity);
            const auto result =
                core.ownerGrant(owner, alice.uuid(), Asset::Coin, 50, "grant", "outbox-grant");
            equal(result.status, Status::Ok);
            const auto events = core.pendingOutbox();
            require(events.size() == 1 && events[0].type == "balance_changed",
                    "Missing durable event");
            {
                ReadDb db(file);
                require(db.query("SELECT count(*) FROM pending_receipt") == "1",
                        "Pending projection absent");
            }
            equal(core.acknowledgeOutbox(events[0].id), Status::Ok);
            equal(core.acknowledgeOutbox(events[0].id), Status::Ok);
            require(core.pendingOutbox().empty(), "Projection acknowledgement failed");
            ReadDb db(file);
            require(db.query("SELECT count(*) FROM pending_receipt") == "0" &&
                        db.query("SELECT count(*) FROM receipts") == "1",
                    "Acknowledgement damaged durable receipt");
            const auto replay =
                core.ownerGrant(owner, alice.uuid(), Asset::Coin, 50, "grant", "outbox-grant");
            require(replay.replayed && replay.receipt == result.receipt &&
                        core.pendingOutbox().empty(),
                    "Replay recreated acknowledged projection");
        });
        test("numbered migration, WAL/FULL, online Native backup and checksum", [&] {
            const auto file = root / "backup.sqlite", copy = root / "native-online-backup.sqlite";
            Core core(file.string(), ownerXuid);
            auto owner = actor(core, ownerIdentity), alice = actor(core, aliceIdentity);
            equal(core.ownerGrant(owner, alice.uuid(), Asset::Coin, 77, "grant", "backup-grant")
                      .status,
                  Status::Ok);
            equal(core.backup(copy.string()), Status::Ok);
            equal(core.backup(copy.string()), Status::Invalid);
            require(fs::exists(copy.string() + ".sha256"), "Backup checksum missing");
            require(read(copy.string() + ".sha256").substr(0, 64) == detail::sha256(read(copy)),
                    "Backup checksum mismatch");
            ReadDb db(copy);
            require(db.query("PRAGMA integrity_check") == "ok" &&
                        db.query("PRAGMA foreign_key_check").empty(),
                    "Backup integrity failed");
            require(db.query("SELECT balance FROM accounts WHERE asset='coin' AND "
                             "uuid='bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb'") == "77",
                    "Backup lost WAL state");
            require(db.query("PRAGMA user_version") == std::to_string(detail::schemaVersion) &&
                        db.query("SELECT count(*) FROM schema_migration") == std::to_string(detail::schemaVersion),
                    "Migration metadata missing");
            ReadDb source(file);
            require(source.query("PRAGMA journal_mode") == "wal" &&
                        source.query("PRAGMA synchronous") == "2",
                    "WAL/FULL not configured");
        });
        test("first Native schema creation does not back up a newly created empty DB", [&] {
            const auto file = root / "fresh-native.sqlite";
            require(!fs::exists(file), "Fresh DB fixture already exists");
            {
                Core core(file.string(), ownerXuid);
                require(migrationBackups(file).empty(), "First creation added an empty DB backup");
                actor(core, ownerIdentity);
            }
            Core reopened(file.string(), ownerXuid);
            require(migrationBackups(file).empty(), "Current schema reopen added a backup");
            ReadDb db(file);
            require(db.query("PRAGMA user_version") == std::to_string(detail::schemaVersion) &&
                        db.query("PRAGMA integrity_check") == "ok" &&
                        db.query("PRAGMA foreign_key_check").empty(),
                    "First schema transaction failed");
        });
        test("a pre-existing Native empty DB is backed up before schema migration", [&] {
            const auto file = root / "existing-empty.sqlite";
            {
                ReadDb db(file, true);
                db.exec("PRAGMA user_version=0");
            }
            require(fs::exists(file), "Existing empty DB fixture was not created");
            {
                Core core(file.string(), ownerXuid);
                const auto copies = migrationBackups(file);
                require(copies.size() == 1, "Existing empty Native DB backup absent or duplicated");
                require(read(copies[0].string() + ".sha256").substr(0, 64) ==
                            detail::sha256(read(copies[0])),
                        "Pre-migration checksum mismatch");
                ReadDb backup(copies[0]);
                require(backup.query("PRAGMA user_version") == "0" &&
                            backup.query("SELECT count(*) FROM sqlite_master WHERE type='table' "
                                         "AND name NOT LIKE 'sqlite_%'") == "0" &&
                            backup.query("PRAGMA integrity_check") == "ok" &&
                            backup.query("PRAGMA foreign_key_check").empty(),
                        "Backup was not a valid pre-migration snapshot");
            }
            Core reopened(file.string(), ownerXuid);
            require(migrationBackups(file).size() == 1, "Schema reopen repeated migration backup");
        });
        test("foreign-key failure prevents declaring backup or reopened DB valid", [&] {
            const auto file = root / "foreign.sqlite", copy = root / "bad-backup.sqlite";
            {
                Core core(file.string(), ownerXuid);
                actor(core, ownerIdentity);
                {
                    ReadDb db(file, true);
                    db.exec("PRAGMA foreign_keys=OFF; INSERT INTO roles(uuid,role) "
                            "VALUES('missing','operations')");
                }
                equal(core.backup(copy.string()), Status::StorageError);
                require(!fs::exists(copy.string() + ".sha256"),
                        "Invalid backup received checksum success marker");
            }
            bool rejected = false;
            try {
                Core bad(file.string(), ownerXuid);
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "Invalid foreign key state was accepted");
        });
        test("different persisted owner and unrecognized existing DB refused", [&] {
            const auto file = root / "owner.sqlite";
            {
                Core core(file.string(), ownerXuid);
            }
            bool rejected = false;
            try {
                Core bad(file.string(), "999");
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "Owner replacement was accepted");
            const auto unknown = root / "unknown-test-fixture.sqlite";
            {
                ReadDb db(unknown, true);
                db.exec("CREATE TABLE unrelated(value TEXT); INSERT INTO unrelated VALUES('do not "
                        "touch')");
            }
            const auto before = read(unknown);
            rejected = false;
            try {
                Core bad(unknown.string(), ownerXuid);
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected && read(unknown) == before, "Unknown DB was changed or accepted");
        });
        std::cout << "PASS " << groups
                  << " groups (exception injection and real process _Exit covered)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
