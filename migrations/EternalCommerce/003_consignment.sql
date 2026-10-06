
CREATE TABLE consignments(id INTEGER PRIMARY KEY,sellerUuid TEXT NOT NULL,item TEXT NOT NULL,nbt TEXT NOT NULL,displayName TEXT NOT NULL,priceMinor INTEGER NOT NULL CHECK(priceMinor>0),listedAtMs INTEGER NOT NULL,expiresAtMs INTEGER NOT NULL,status TEXT NOT NULL DEFAULT 'listed' CHECK(status IN ('listed','sold','returned','cancelled')),buyerUuid TEXT,soldAtMs INTEGER) STRICT;
CREATE INDEX consignments_status ON consignments(status,expiresAtMs);
PRAGMA user_version=3;
