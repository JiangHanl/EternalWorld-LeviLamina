
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL) STRICT;
CREATE TABLE schema_migration(version INTEGER PRIMARY KEY,checksum TEXT NOT NULL,appliedAt INTEGER NOT NULL) STRICT;
CREATE TABLE players(uuid TEXT PRIMARY KEY,xuid TEXT NOT NULL UNIQUE) STRICT;
CREATE TABLE accounts(uuid TEXT NOT NULL REFERENCES players(uuid),asset TEXT NOT NULL CHECK(asset IN ('coin','reputation')),balance INTEGER NOT NULL DEFAULT 0 CHECK(balance>=0),PRIMARY KEY(uuid,asset)) STRICT;
CREATE TABLE roles(uuid TEXT NOT NULL REFERENCES players(uuid),role TEXT NOT NULL CHECK(role IN ('build','economy','law','content','resources','operations')),PRIMARY KEY(uuid,role)) STRICT;
CREATE TABLE transactions(transactionId TEXT PRIMARY KEY,executorScope TEXT NOT NULL CHECK(executorScope='core.domain.v1'),idempotencyKey TEXT NOT NULL,status TEXT NOT NULL CHECK(status IN ('committed')),createdAt INTEGER NOT NULL,updatedAt INTEGER NOT NULL,payload TEXT NOT NULL,requestHash TEXT NOT NULL,operation TEXT NOT NULL,actorUuid TEXT NOT NULL REFERENCES players(uuid),retryCount INTEGER NOT NULL DEFAULT 0 CHECK(retryCount>=0),UNIQUE(executorScope,actorUuid,idempotencyKey)) STRICT;
CREATE TABLE entries(id INTEGER PRIMARY KEY,transactionId TEXT NOT NULL REFERENCES transactions(transactionId),uuid TEXT NOT NULL REFERENCES players(uuid),asset TEXT NOT NULL CHECK(asset IN ('coin','reputation')),delta INTEGER NOT NULL,balanceAfter INTEGER NOT NULL CHECK(balanceAfter>=0)) STRICT;
CREATE TABLE receipts(transactionId TEXT PRIMARY KEY REFERENCES transactions(transactionId),requestHash TEXT NOT NULL,payload TEXT NOT NULL) STRICT;
CREATE TABLE audit(id INTEGER PRIMARY KEY,transactionId TEXT NOT NULL REFERENCES transactions(transactionId),actorUuid TEXT NOT NULL REFERENCES players(uuid),operation TEXT NOT NULL,reason TEXT NOT NULL,requestHash TEXT NOT NULL,createdAt INTEGER NOT NULL) STRICT;
CREATE TABLE outbox(id INTEGER PRIMARY KEY,transactionId TEXT NOT NULL REFERENCES transactions(transactionId),targetUuid TEXT NOT NULL REFERENCES players(uuid),eventType TEXT NOT NULL,payload TEXT NOT NULL,acknowledged INTEGER NOT NULL DEFAULT 0 CHECK(acknowledged IN (0,1)),createdAt INTEGER NOT NULL,acknowledgedAt INTEGER) STRICT;
CREATE INDEX outbox_pending ON outbox(acknowledged,id);
CREATE TABLE pending_receipt(transactionId TEXT PRIMARY KEY REFERENCES transactions(transactionId),status TEXT NOT NULL CHECK(status='pending_projection'),payload TEXT NOT NULL,createdAt INTEGER NOT NULL,updatedAt INTEGER NOT NULL,retryCount INTEGER NOT NULL DEFAULT 0 CHECK(retryCount>=0)) STRICT;
PRAGMA user_version=1;
