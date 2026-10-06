#include "Commerce.hpp"
#include "Schema.hpp"
#include "Sha256.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace eternal::commerce;
namespace fs = std::filesystem;
namespace {
int groups{};
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
const char *name(Status status) {
    switch (status) {
    case Status::Ok: return "Ok";
    case Status::Busy: return "Busy";
    case Status::Invalid: return "Invalid";
    case Status::Overflow: return "Overflow";
    case Status::Conflict: return "Conflict";
    case Status::NotFound: return "NotFound";
    case Status::Expired: return "Expired";
    case Status::StorageError: return "StorageError";
    }
    return "Unknown";
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
std::string read(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Test file missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    const auto suffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root = fs::temp_directory_path() / ("EternalCommerceTests-" + suffix);
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
                         "migrations/EternalCommerce/001_initial.sql";
        require(read(sql) == detail::schemaV1, "SQL and compiled schema differ");
        const auto sql2 = fs::path(__FILE__).parent_path().parent_path().parent_path() /
                          "migrations/EternalCommerce/002_transfer_tax.sql";
        require(read(sql2) == detail::schemaV2, "SQL and compiled schema v2 differ");
    });

    test("gift validation and 2% fee", [&] {
        Commerce commerce((root / "gift.sqlite").string());
        auto ok = commerce.createGift("creator", 10 * minorUnitsPerLiang, 3, 1000);
        equal(ok.status, Status::Ok);
        require(ok.gift.has_value(), "Missing gift");
        require(ok.gift->totalMinor == 30 * minorUnitsPerLiang, "Wrong total");
        require(ok.gift->feeMinor == 30 * minorUnitsPerLiang * 2 / 100, "Wrong fee");
        require(ok.gift->status == "open", "Wrong status");
        require(ok.gift->expiresAtMs == 1000 + giftLifetimeMs, "Wrong expiry");
        equal(commerce.createGift("creator", 9 * minorUnitsPerLiang, 3, 1000).status,
              Status::Invalid);
        equal(commerce.createGift("creator", 10 * minorUnitsPerLiang, 2, 1000).status,
              Status::Invalid);
        equal(commerce.createGift("creator", 10 * minorUnitsPerLiang, 21, 1000).status,
              Status::Invalid);
        equal(commerce.createGift("creator", 2000 * minorUnitsPerLiang, 3, 1000).status,
              Status::Overflow);
    });

    test("gift expires only after its lifetime", [&] {
        Commerce commerce((root / "gift-expire.sqlite").string());
        auto ok = commerce.createGift("creator", 10 * minorUnitsPerLiang, 3, 1000);
        equal(ok.status, Status::Ok);
        const auto id = ok.gift->id;
        equal(commerce.expireGift(id, 1000 + giftLifetimeMs - 1), Status::Ok);
        require(commerce.gift(id).gift->status == "open", "Expired before lifetime");
        equal(commerce.expireGift(id, 1000 + giftLifetimeMs), Status::Ok);
        require(commerce.gift(id).gift->status == "expired", "Did not expire");
        equal(commerce.expireGift(id, 1000 + giftLifetimeMs + 1), Status::Conflict);
    });

    test("gift claim enforces one share per player and settles", [&] {
        Commerce commerce((root / "gift-claim.sqlite").string());
        auto ok = commerce.createGift("creator", 10 * minorUnitsPerLiang, 3, 1000);
        equal(ok.status, Status::Ok);
        const auto id = ok.gift->id;
        equal(commerce.claimGift(id, "a", 1000), Status::Ok);
        equal(commerce.claimGift(id, "a", 1000), Status::Conflict);
        equal(commerce.claimGift(id, "b", 1000), Status::Ok);
        equal(commerce.claimGift(id, "c", 1000), Status::Ok);
        require(commerce.gift(id).gift->status == "settled", "Not settled when full");
        equal(commerce.claimGift(id, "d", 1000), Status::Conflict);
        auto g2 = commerce.createGift("creator", 10 * minorUnitsPerLiang, 3, 1000);
        equal(g2.status, Status::Ok);
        equal(commerce.claimGift(g2.gift->id, "a", 1000 + giftLifetimeMs), Status::Expired);
    });

    test("gift and claimed participant survive restart", [&] {
        std::int64_t id{};
        {
            Commerce commerce((root / "gift-restart.sqlite").string());
            auto ok = commerce.createGift("creator", 10 * minorUnitsPerLiang, 3, 1000);
            equal(ok.status, Status::Ok);
            equal(commerce.claimGift(ok.gift->id, "a", 1000), Status::Ok);
            id = ok.gift->id;
        }
        Commerce commerce((root / "gift-restart.sqlite").string());
        auto g = commerce.gift(id);
        equal(g.status, Status::Ok);
        require(g.gift->status == "open", "Restart changed status");
        equal(commerce.claimGift(id, "a", 1000), Status::Conflict);
    });

    test("tiered transfer tax applies brackets in order", [&] {
        const std::array<TaxTier, 3> tiers{{
            {1000, 20},
            {3000, 50},
            {0, 100},
        }};
        require(computeTieredTax(0, tiers) == 0, "Zero taxable");
        require(computeTieredTax(1000, tiers) == 20, "First bracket");
        require(computeTieredTax(2000, tiers) == 70, "Two brackets");
        require(computeTieredTax(4000, tiers) == 220, "All brackets");
    });

    test("transfer tax respects daily cumulative allowance", [&] {
        Commerce commerce((root / "transfer-allowance.sqlite").string());
        auto r1 = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k1", 1000000);
        equal(r1.status, Status::Ok);
        require(r1.taxFreeMinor == 500 * minorUnitsPerLiang && r1.taxableMinor == 0 &&
                    r1.taxMinor == 0 && r1.netMinor == 500 * minorUnitsPerLiang,
                "First transfer should be tax-free");
        auto r2 = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k2", 1000000);
        equal(r2.status, Status::Ok);
        require(r2.taxFreeMinor == 500 * minorUnitsPerLiang && r2.taxMinor == 0,
                "Cumulative allowance not applied");
        auto r3 = commerce.recordTransfer("alice", 100 * minorUnitsPerLiang, "k3", 1000000);
        equal(r3.status, Status::Ok);
        require(r3.taxFreeMinor == 0 && r3.taxableMinor == 100 * minorUnitsPerLiang,
                "Allowance exhausted");
        require(r3.taxMinor == 100 * minorUnitsPerLiang * 2 / 100, "Wrong tax rate");
        require(r3.netMinor == 100 * minorUnitsPerLiang - r3.taxMinor, "Wrong net");
    });

    test("transfer tax replay is idempotent", [&] {
        Commerce commerce((root / "transfer-idem.sqlite").string());
        auto r1 = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k1", 1000000);
        equal(r1.status, Status::Ok);
        auto r2 = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k1", 1000000);
        equal(r2.status, Status::Ok);
        require(r2.replayed && r2.transferId == r1.transferId, "Not replayed");
        require(r2.taxFreeMinor == r1.taxFreeMinor && r2.taxMinor == r1.taxMinor,
                "Replay changed result");
        auto r3 = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k2", 1000000);
        require(r3.taxFreeMinor == 500 * minorUnitsPerLiang, "Allowance double-counted");
    });

    test("transfer allowance resets on the next UTC+8 day", [&] {
        Commerce commerce((root / "transfer-day.sqlite").string());
        constexpr std::int64_t dayMs = 24ll * 3600 * 1000;
        auto r1 = commerce.recordTransfer("alice", 1000 * minorUnitsPerLiang, "k1", 1000000);
        equal(r1.status, Status::Ok);
        require(r1.taxFreeMinor == 1000 * minorUnitsPerLiang, "First day not fully exempt");
        auto r2 = commerce.recordTransfer("alice", 1000 * minorUnitsPerLiang, "k2",
                                          1000000 + dayMs);
        equal(r2.status, Status::Ok);
        require(r2.taxFreeMinor == 1000 * minorUnitsPerLiang, "Allowance did not reset");
    });

    test("preview transfer does not consume allowance", [&] {
        Commerce commerce((root / "transfer-preview.sqlite").string());
        auto p = commerce.previewTransfer("alice", 500 * minorUnitsPerLiang, 1000000);
        equal(p.status, Status::Ok);
        require(p.taxFreeMinor == 500 * minorUnitsPerLiang, "Wrong preview");
        auto r = commerce.recordTransfer("alice", 500 * minorUnitsPerLiang, "k1", 1000000);
        require(r.taxFreeMinor == 500 * minorUnitsPerLiang, "Preview consumed allowance");
    });

    std::cout << "Commerce Domain: " << groups
              << " groups passed; no Core, BDS or player validation performed.\n";
    return 0;
}
