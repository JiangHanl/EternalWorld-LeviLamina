
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL) STRICT;
CREATE TABLE schema_migration(version INTEGER PRIMARY KEY,checksum TEXT NOT NULL,appliedAt INTEGER NOT NULL) STRICT;
CREATE TABLE gifts(id INTEGER PRIMARY KEY,creatorUuid TEXT NOT NULL,shareMinor INTEGER NOT NULL CHECK(shareMinor>=1000),recipients INTEGER NOT NULL CHECK(recipients>=3 AND recipients<=20),totalMinor INTEGER NOT NULL CHECK(totalMinor>0),feeMinor INTEGER NOT NULL CHECK(feeMinor>=0),expiresAtMs INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','settled','expired')),createdAtMs INTEGER NOT NULL) STRICT;
CREATE TABLE gift_participants(giftId INTEGER NOT NULL REFERENCES gifts(id),playerUuid TEXT NOT NULL,shareMinor INTEGER NOT NULL CHECK(shareMinor>=1000),claimedAtMs INTEGER NOT NULL,PRIMARY KEY(giftId,playerUuid)) STRICT;
CREATE INDEX gifts_open ON gifts(status,expiresAtMs);
PRAGMA user_version=1;
