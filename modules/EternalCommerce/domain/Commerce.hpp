#pragma once

#include <cstdint>
#include <memory>
#include <optional>
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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eternal::commerce
