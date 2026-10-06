
CREATE TABLE delivery_requests(id INTEGER PRIMARY KEY,playerUuid TEXT NOT NULL,item TEXT NOT NULL,nbt TEXT NOT NULL,source TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','delivering','delivered','reconciling')),attemptCount INTEGER NOT NULL DEFAULT 0 CHECK(attemptCount>=0),createdAtMs INTEGER NOT NULL,updatedAtMs INTEGER NOT NULL,lastError TEXT NOT NULL DEFAULT '') STRICT;
CREATE INDEX delivery_pending ON delivery_requests(status,updatedAtMs);
PRAGMA user_version=5;
