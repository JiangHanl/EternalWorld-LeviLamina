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

    std::cout << "Commerce Domain: " << groups
              << " groups passed; no Core, BDS or player validation performed.\n";
    return 0;
}
