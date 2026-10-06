#pragma once
#include <string_view>

namespace eternal::commerce::detail {
inline constexpr int schemaVersion = 4;
inline constexpr std::string_view schemaV1 = R"SQL(
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL) STRICT;
CREATE TABLE schema_migration(version INTEGER PRIMARY KEY,checksum TEXT NOT NULL,appliedAt INTEGER NOT NULL) STRICT;
CREATE TABLE gifts(id INTEGER PRIMARY KEY,creatorUuid TEXT NOT NULL,shareMinor INTEGER NOT NULL CHECK(shareMinor>=1000),recipients INTEGER NOT NULL CHECK(recipients>=3 AND recipients<=20),totalMinor INTEGER NOT NULL CHECK(totalMinor>0),feeMinor INTEGER NOT NULL CHECK(feeMinor>=0),expiresAtMs INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','settled','expired')),createdAtMs INTEGER NOT NULL) STRICT;
CREATE TABLE gift_participants(giftId INTEGER NOT NULL REFERENCES gifts(id),playerUuid TEXT NOT NULL,shareMinor INTEGER NOT NULL CHECK(shareMinor>=1000),claimedAtMs INTEGER NOT NULL,PRIMARY KEY(giftId,playerUuid)) STRICT;
CREATE INDEX gifts_open ON gifts(status,expiresAtMs);
PRAGMA user_version=1;
)SQL";
inline constexpr std::string_view schemaV2 = R"SQL(
CREATE TABLE transfer_allowance(playerUuid TEXT NOT NULL,day INTEGER NOT NULL,exemptUsedMinor INTEGER NOT NULL DEFAULT 0 CHECK(exemptUsedMinor>=0),PRIMARY KEY(playerUuid,day)) STRICT;
CREATE TABLE transfers(id INTEGER PRIMARY KEY,senderUuid TEXT NOT NULL,amountMinor INTEGER NOT NULL CHECK(amountMinor>0),taxFreeMinor INTEGER NOT NULL CHECK(taxFreeMinor>=0),taxableMinor INTEGER NOT NULL CHECK(taxableMinor>=0),taxMinor INTEGER NOT NULL CHECK(taxMinor>=0),netMinor INTEGER NOT NULL CHECK(netMinor>=0),idempotencyKey TEXT NOT NULL,day INTEGER NOT NULL,createdAtMs INTEGER NOT NULL,UNIQUE(senderUuid,idempotencyKey)) STRICT;
PRAGMA user_version=2;
)SQL";
inline constexpr std::string_view schemaV3 = R"SQL(
CREATE TABLE consignments(id INTEGER PRIMARY KEY,sellerUuid TEXT NOT NULL,item TEXT NOT NULL,nbt TEXT NOT NULL,displayName TEXT NOT NULL,priceMinor INTEGER NOT NULL CHECK(priceMinor>0),listedAtMs INTEGER NOT NULL,expiresAtMs INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'listed' CHECK(status IN ('listed','sold','returned','cancelled')),buyerUuid TEXT,soldAtMs INTEGER) STRICT;
CREATE INDEX consignments_status ON consignments(status,expiresAtMs);
PRAGMA user_version=3;
)SQL";
inline constexpr std::string_view schemaV4 = R"SQL(
CREATE TABLE acquisition_quota(playerUuid TEXT NOT NULL,day INTEGER NOT NULL,usedMinor INTEGER NOT NULL DEFAULT 0 CHECK(usedMinor>=0),PRIMARY KEY(playerUuid,day)) STRICT;
CREATE TABLE acquisitions(id INTEGER PRIMARY KEY,requesterUuid TEXT NOT NULL,item TEXT NOT NULL,amountMinor INTEGER NOT NULL CHECK(amountMinor>0),idempotencyKey TEXT NOT NULL,day INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','fulfilled','cancelled')),createdAtMs INTEGER NOT NULL,UNIQUE(requesterUuid,idempotencyKey)) STRICT;
PRAGMA user_version=4;
)SQL";
} // namespace eternal::commerce::detail
