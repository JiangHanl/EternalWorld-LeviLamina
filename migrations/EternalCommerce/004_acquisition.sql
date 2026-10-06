
CREATE TABLE acquisition_quota(playerUuid TEXT NOT NULL,day INTEGER NOT NULL,usedMinor INTEGER NOT NULL DEFAULT 0 CHECK(usedMinor>=0),PRIMARY KEY(playerUuid,day)) STRICT;
CREATE TABLE acquisitions(id INTEGER PRIMARY KEY,requesterUuid TEXT NOT NULL,item TEXT NOT NULL,amountMinor INTEGER NOT NULL CHECK(amountMinor>0),idempotencyKey TEXT NOT NULL,day INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','fulfilled','cancelled')),createdAtMs INTEGER NOT NULL,UNIQUE(requesterUuid,idempotencyKey)) STRICT;
PRAGMA user_version=4;
