
CREATE TABLE transfer_allowance(playerUuid TEXT NOT NULL,day INTEGER NOT NULL,exemptUsedMinor INTEGER NOT NULL DEFAULT 0 CHECK(exemptUsedMinor>=0),PRIMARY KEY(playerUuid,day)) STRICT;
CREATE TABLE transfers(id INTEGER PRIMARY KEY,senderUuid TEXT NOT NULL,amountMinor INTEGER NOT NULL CHECK(amountMinor>0),taxFreeMinor INTEGER NOT NULL CHECK(taxFreeMinor>=0),taxableMinor INTEGER NOT NULL CHECK(taxableMinor>=0),taxMinor INTEGER NOT NULL CHECK(taxMinor>=0),netMinor INTEGER NOT NULL CHECK(netMinor>=0),idempotencyKey TEXT NOT NULL,day INTEGER NOT NULL,createdAtMs INTEGER NOT NULL,UNIQUE(senderUuid,idempotencyKey)) STRICT;
PRAGMA user_version=2;
