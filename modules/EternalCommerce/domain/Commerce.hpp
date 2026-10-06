#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace eternal::commerce {

// Commerce uses Core minor units (cents): 100 cents == 1 liang.
inline constexpr std::int64_t minorUnitsPerLiang = 100;
inline constexpr std::int64_t giftMinShareMinor = 10 * minorUnitsPerLiang;
inline constexpr std::int64_t giftMaxTotalMinor = 3000 * minorUnitsPerLiang;
inline constexpr std::int64_t giftMinRecipients = 3;
inline constexpr std::int64_t giftMaxRecipients = 20;
inline constexpr std::int64_t giftFeePermille = 20;
inline constexpr std::int64_t giftLifetimeMs = 300000;
inline constexpr std::int64_t transferDailyExemptMinor = 1000 * minorUnitsPerLiang;
inline constexpr std::int64_t transferMinSplitMinor = 5 * minorUnitsPerLiang;

enum class Status {
    Ok,
    Busy,
    Invalid,
    Overflow,
    Conflict,
    NotFound,
    Expired,
    StorageError
};

struct Gift {
    std::int64_t id{};
    std::string creatorUuid;
    std::int64_t shareMinor{};
    std::int64_t recipients{};
    std::int64_t totalMinor{};
    std::int64_t feeMinor{};
    std::int64_t expiresAtMs{};
    std::string status;
    std::int64_t createdAtMs{};
};

struct GiftResult {
    Status status{Status::Invalid};
    std::optional<Gift> gift;
    std::string error;
};

struct TaxTier {
    std::int64_t upToMinor{};   // Cumulative exclusive upper bound; <=0 means remainder.
    std::int32_t ratePermille{}; // 20 == 2%, 30 == 3%, 50 == 5%.
};

// Placeholder tier boundaries pending the v3.3.3 product spec. The 2/2/3/5% rates are
// recorded; the exact bracket edges are not yet authoritative and must be revisited.
inline constexpr std::array<TaxTier, 4> transferDefaultTiers{{
    {1000 * minorUnitsPerLiang, 20},
    {3000 * minorUnitsPerLiang, 20},
    {10000 * minorUnitsPerLiang, 30},
    {0, 50},
}};

struct TransferResult {
    Status status{Status::Invalid};
    std::int64_t transferId{};
    std::int64_t taxFreeMinor{};
    std::int64_t taxableMinor{};
    std::int64_t taxMinor{};
    std::int64_t netMinor{};
    bool replayed{};
    std::string error;
};

std::int64_t computeTieredTax(std::int64_t taxableMinor, std::span<const TaxTier> tiers);

class Commerce {
  public:
    explicit Commerce(std::string dbPath);
    ~Commerce();
    Commerce(const Commerce &) = delete;
    Commerce &operator=(const Commerce &) = delete;

    GiftResult createGift(std::string_view creatorUuid, std::int64_t shareMinor,
                          std::int64_t recipients, std::int64_t nowMs);
    GiftResult gift(std::int64_t id) const;
    Status claimGift(std::int64_t id, std::string_view playerUuid, std::int64_t nowMs);
    Status expireGift(std::int64_t id, std::int64_t nowMs);
    TransferResult recordTransfer(std::string_view senderUuid, std::int64_t amountMinor,
                                  std::string_view idempotencyKey, std::int64_t nowMs);
    TransferResult previewTransfer(std::string_view senderUuid, std::int64_t amountMinor,
                                   std::int64_t nowMs) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eternal::commerce
