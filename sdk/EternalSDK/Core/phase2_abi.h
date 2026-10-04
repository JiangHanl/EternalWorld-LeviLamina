#ifndef ETERNALSDK_PHASE2_ABI_H
#define ETERNALSDK_PHASE2_ABI_H

#include "core_abi.h"

/* Independent 1.1 service. Never replace or extend the stored 1.0 table in
 * place. Its complete 96-byte prefix and all original DTO layouts remain fixed.
 * Caller/context/capability values are Core-issued opaque, unpredictable IDs.
 * No public function issues trusted player identity, module scope or Owner.
 * A registered module ID, native OP, a role snapshot or a supplied XUID grants
 * nothing. Core validates live context, capability, player session/role versions,
 * operation/target/asset/amount scope, budgets, expiry and revocation on every call.
 */
#define EC_PHASE2_SERVICE_ID "EternalCore.Phase2Api"
#define EC_PHASE2_API_MAJOR UINT32_C(1)
#define EC_PHASE2_API_MINOR UINT32_C(1)
#define EC_PHASE2_STRUCT_VERSION UINT32_C(1)
#define EC_PHASE2_PRODUCTION_FEATURES UINT64_C(0)
#define EC_PHASE2_OUTBOX_TOPIC "core.outbox.changed"
#define EC_PHASE2_MAX_NAME_UTF8_BYTES UINT32_C(128)
#define EC_PHASE2_MAX_OUTBOX_PAGE UINT32_C(100)

#define EC_P2_FEATURE_IDENTITY (UINT64_C(1)<<0)
#define EC_P2_FEATURE_ROLES (UINT64_C(1)<<1)
#define EC_P2_FEATURE_PERMISSIONS (UINT64_C(1)<<2)
#define EC_P2_FEATURE_ASSET_READ (UINT64_C(1)<<3)
#define EC_P2_FEATURE_ASSET_MUTATION (UINT64_C(1)<<4)
#define EC_P2_FEATURE_RECEIPTS (UINT64_C(1)<<5)
#define EC_P2_FEATURE_OUTBOX (UINT64_C(1)<<6)
#define EC_P2_FEATURE_ALL UINT64_C(127)

#define EC_P2_ROLE_BUILD (UINT64_C(1)<<0)
#define EC_P2_ROLE_ECONOMY (UINT64_C(1)<<1)
#define EC_P2_ROLE_LAW (UINT64_C(1)<<2)
#define EC_P2_ROLE_CONTENT (UINT64_C(1)<<3)
#define EC_P2_ROLE_RESOURCES (UINT64_C(1)<<4)
#define EC_P2_ROLE_OPERATIONS (UINT64_C(1)<<5)
#define EC_P2_ROLE_OWNER (UINT64_C(1)<<6)
#define EC_P2_ROLE_ADMINISTRATOR (UINT64_C(1)<<7)
#define EC_P2_ROLE_CONTENT_MANAGER (UINT64_C(1)<<8)
#define EC_P2_ROLE_ECONOMY_MANAGER (UINT64_C(1)<<9)
#define EC_P2_ROLE_BUILDER (UINT64_C(1)<<10)
#define EC_P2_ROLE_MODERATOR (UINT64_C(1)<<11)
#define EC_P2_ROLE_PLAYER (UINT64_C(1)<<12)
#define EC_P2_ROLE_ALL UINT64_C(8191)
#define EC_P2_ROLE_MUTABLE_ALL UINT64_C(4031)

/* Host-approved module capabilities are a separate, intersecting boundary.
 * They never substitute for player authorization, request scope or budgets. */
#define EC_MODULE_CAP_PLAYER_READ (UINT64_C(1)<<0)
#define EC_MODULE_CAP_PERMISSION_CHECK (UINT64_C(1)<<1)
#define EC_MODULE_CAP_MONEY (UINT64_C(1)<<2)
#define EC_MODULE_CAP_REPUTATION (UINT64_C(1)<<3)
#define EC_MODULE_CAP_AUDIT (UINT64_C(1)<<4)
#define EC_MODULE_CAP_EVENTS (UINT64_C(1)<<5)
#define EC_MODULE_CAP_ALL UINT64_C(63)
#define EC_MODULE_CAP_PLAYER_READ_NAME "core.player.read.v1"
#define EC_MODULE_CAP_PERMISSION_CHECK_NAME "core.permission.check.v1"
#define EC_MODULE_CAP_MONEY_NAME "core.transaction.money.v1"
#define EC_MODULE_CAP_REPUTATION_NAME "core.transaction.reputation.v1"
#define EC_MODULE_CAP_AUDIT_NAME "core.audit.write.v1"
#define EC_MODULE_CAP_EVENTS_NAME "core.events.v1"

enum {
    EC_P2_OP_IDENTITY_READ=1, EC_P2_OP_ROLE_READ=2,
    EC_P2_OP_PERMISSION_CHECK=3, EC_P2_OP_ASSET_READ=4,
    EC_P2_OP_ASSET_ADD=5, EC_P2_OP_ASSET_DEDUCT=6,
    EC_P2_OP_TRANSFER=7, EC_P2_OP_ROLE_GRANT=8,
    EC_P2_OP_ROLE_REVOKE=9, EC_P2_OP_RECEIPT_READ=10,
    EC_P2_OP_OUTBOX_READ=11,
    EC_P2_ASSET_NONE=0, EC_P2_ASSET_MONEY=1, EC_P2_ASSET_REPUTATION=2,
    EC_P2_IDENTITY_ONLINE=1, EC_P2_IDENTITY_OWNER=2,
    EC_P2_IDENTITY_NATIVE_OP=4, EC_P2_IDENTITY_SYNTHETIC=8,
    EC_P2_SUBMISSION_REPLAYED=1,
    EC_P2_OUTBOX_ASSET_CHANGED=1, EC_P2_OUTBOX_ROLE_CHANGED=2
};
/* Permission mask uses bit (operation - 1). Unknown bits must be rejected. */
#define EC_P2_PERMISSION_ALL UINT64_C(2047)
#define EC_P2_ASSET_MASK_MONEY (UINT64_C(1)<<0)
#define EC_P2_ASSET_MASK_REPUTATION (UINT64_C(1)<<1)

#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 8)
typedef EcId128 EcCallerContext;
typedef EcId128 EcPlayerId;
typedef EcId128 EcRequestId;

typedef struct EcPhase2RequestMeta {
    /* When embedded at offset zero, size describes the COMPLETE request, not
     * sizeof(EcPhase2RequestMeta). Each endpoint validates its exact DTO size. */
    uint32_t struct_size;
    uint32_t struct_version;
    EcCallerContext caller_context;
    EcCapability capability;
    EcRequestId request_id;
    EcIdempotencyKey idempotency_key;
    uint64_t expected_revision;
    uint64_t reserved;
} EcPhase2RequestMeta;

/* Read-only requests may leave request/idempotency IDs zero. Mutations require
 * nonzero IDs. Core owns the fixed idempotency namespace, verified actor and
 * registered caller binding; callers cannot select those namespace components.
 * expected_revision=0 means no precondition only when the issued scope allows it.
 */
typedef struct EcPhase2QueryRequest {
    EcPhase2RequestMeta meta;
    EcPlayerId target;
    uint64_t reserved;
} EcPhase2QueryRequest;

typedef struct EcPhase2AssetRequest {
    EcPhase2RequestMeta meta;
    EcPlayerId target;
    uint32_t asset;
    uint32_t reserved0;
    uint64_t reserved1;
} EcPhase2AssetRequest;

typedef struct EcPhase2PermissionRequest {
    EcPhase2RequestMeta meta;
    EcPlayerId subject;
    EcPlayerId target;
    uint32_t operation;
    uint32_t asset;
    int64_t minor_units;
    uint64_t role_mask;
    uint64_t reserved;
} EcPhase2PermissionRequest;

typedef struct EcPhase2MutationRequest {
    EcPhase2RequestMeta meta;
    EcPlayerId target; /* Transfer debit account; must match authorized scope. */
    EcPlayerId recipient; /* Transfer only; zero for other operations. */
    uint32_t operation;
    uint32_t asset;
    int64_t minor_units; /* Positive ADD/DEDUCT/TRANSFER; zero for role changes. */
    uint64_t role_mask; /* Exactly one mutable role bit for GRANT/REVOKE. */
    EcUtf8View reason; /* Required valid UTF-8, 1..256 bytes, no NUL/controls. */
    uint64_t reserved[2];
} EcPhase2MutationRequest;

/* Money uses 100 minor units per liang. Reputation uses indivisible integers,
 * is not money, and cannot be transferred. No Commerce tax rule is defined here.
 * Owner is informational/configured; Player is implicit/read-only. Neither is
 * mutable through this API. Irrelevant fields must be zero. Flags/revisions are information,
 * never bearer grants. PlayerId is persistent Core identity, not an address.
 */
typedef struct EcPhase2IdentitySnapshot {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPlayerId player_id;
    EcUuid uuid; /* Canonical UUID bytes authenticated by the native ingress. */
    uint64_t xuid;
    uint64_t first_seen_unix_ms;
    uint64_t last_seen_unix_ms;
    uint64_t identity_revision;
    uint64_t role_revision;
    uint64_t session_generation;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
} EcPhase2IdentitySnapshot;

typedef struct EcPhase2RoleSnapshot {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPlayerId player_id;
    uint64_t role_mask;
    uint64_t permission_mask;
    uint64_t role_revision;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
} EcPhase2RoleSnapshot;

typedef struct EcPhase2AssetSnapshot {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPlayerId player_id;
    uint32_t asset;
    uint32_t flags;
    int64_t minor_units;
    uint64_t account_revision;
    uint64_t reserved;
} EcPhase2AssetSnapshot;

typedef struct EcPhase2PermissionDecision {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPlayerId subject;
    uint32_t allowed;
    EcStatus result;
    uint64_t role_revision;
    uint64_t identity_revision;
    uint64_t session_generation;
    uint64_t reserved;
} EcPhase2PermissionDecision;

typedef struct EcPhase2FeatureInfo {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t enabled; /* Production only; initially zero until real validation. */
    uint64_t implemented;
    uint64_t development_only; /* NOT a grant or a production feature fallback. */
    uint64_t instance_epoch;
    uint32_t lifecycle;
    uint32_t domain_thread_model;
    uint64_t reserved[2];
} EcPhase2FeatureInfo;

typedef struct EcPhase2Submission {
    uint32_t struct_size;
    uint32_t struct_version;
    EcReceiptId receipt_id;
    EcRequestId request_id;
    uint32_t state;
    EcStatus result;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t accepted_at_unix_ms;
    uint64_t reserved1;
} EcPhase2Submission;

typedef struct EcPhase2ReceiptRequest {
    EcPhase2RequestMeta meta;
    EcReceiptId receipt_id;
    uint64_t reserved;
} EcPhase2ReceiptRequest;

typedef struct EcPhase2Receipt {
    uint32_t struct_size;
    uint32_t struct_version;
    EcReceiptId receipt_id;
    EcRequestId request_id;
    uint32_t state;
    EcStatus result;
    uint32_t operation;
    uint32_t asset;
    EcPlayerId target;
    EcPlayerId recipient;
    int64_t minor_units;
    uint64_t role_mask;
    int64_t target_balance;
    int64_t recipient_balance;
    uint64_t target_revision;
    uint64_t recipient_revision;
    uint64_t ledger_revision;
    uint64_t audit_revision;
    uint64_t completed_at_unix_ms;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
} EcPhase2Receipt;

typedef struct EcPhase2OutboxRequest {
    EcPhase2RequestMeta meta;
    uint64_t after_event_id;
    uint32_t limit; /* 1..100. Core also filters rows by current authorization. */
    uint32_t reserved0;
    uint64_t reserved1;
} EcPhase2OutboxRequest;

typedef struct EcPhase2OutboxEvent {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t event_id;
    EcReceiptId receipt_id;
    EcRequestId request_id;
    EcPlayerId target;
    uint32_t asset;
    uint32_t type;
    int64_t authoritative_balance;
    uint64_t account_revision;
    uint64_t occurred_at_unix_ms;
    uint64_t reserved;
} EcPhase2OutboxEvent;

typedef struct EcPhase2OutboxBuffer {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPhase2OutboxEvent* data; /* Caller-owned; Host/Core never free this buffer. */
    uint32_t capacity;
    uint32_t count;
    uint32_t required;
    uint32_t reserved0;
    uint64_t next_event_id;
    uint64_t reserved1;
} EcPhase2OutboxBuffer;

/* Host's general EventBus is not an authorization boundary. Only this minimal
 * notification may be broadcast. Never publish full identities/balances/tokens
 * in it. Authorized consumers obtain current rows through read_outbox. No public
 * acknowledgement method exists; only Core's trusted projection worker may ack.
 */
typedef struct EcPhase2OutboxNotice {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t event_cursor;
    uint64_t instance_epoch;
    uint64_t reserved;
} EcPhase2OutboxNotice;

typedef EcStatus (EC_CALL *EcGetPhase2FeaturesFn)(EcPhase2FeatureInfo*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadPhase2IdentityFn)(const EcPhase2QueryRequest*, EcPhase2IdentitySnapshot*, EcUtf8Buffer*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadPhase2RolesFn)(const EcPhase2QueryRequest*, EcPhase2RoleSnapshot*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcCheckPhase2PermissionFn)(const EcPhase2PermissionRequest*, EcPhase2PermissionDecision*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadPhase2AssetFn)(const EcPhase2AssetRequest*, EcPhase2AssetSnapshot*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcSubmitPhase2MutationFn)(const EcPhase2MutationRequest*, EcPhase2Submission*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcPollPhase2ReceiptFn)(const EcPhase2ReceiptRequest*, EcPhase2Receipt*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadPhase2OutboxFn)(const EcPhase2OutboxRequest*, EcPhase2OutboxBuffer*) EC_NOEXCEPT;

typedef struct EternalCorePhase2Api {
    EternalCoreApi v1_0; /* Unmodified 96-byte legacy table, including features=0. */
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t api_major;
    uint32_t api_minor;
    EcCallerContext caller_context; /* Zero on unbound discovery/diagnostic table. */
    uint64_t caller_generation;
    uint64_t instance_epoch;
    EcGetPhase2FeaturesFn get_phase2_features;
    EcReadPhase2IdentityFn read_identity_v2;
    EcReadPhase2RolesFn read_roles;
    EcCheckPhase2PermissionFn check_permission;
    EcReadPhase2AssetFn read_asset;
    EcSubmitPhase2MutationFn submit_mutation;
    EcPollPhase2ReceiptFn poll_receipt_v2;
    EcReadPhase2OutboxFn read_outbox;
    uint64_t reserved[4];
} EternalCorePhase2Api;

/* Core owns a scoped table until that module binding is revoked/disabled or Core
 * disables. The consumer must discard the pointer and tokens on Disable and
 * rediscover after Enable. Never save pointers in persistent data. A shared
 * table/caller string cannot create a scoped binding. Native private ingress
 * plus the Host's authenticated Node query path must establish it first.
 */
#pragma pack(pop)
EC_STATIC_ASSERT(sizeof(EcPhase2RequestMeta)==88, "Phase2 metadata layout");
EC_STATIC_ASSERT(sizeof(EcPhase2QueryRequest)==112, "Phase2 query layout");
EC_STATIC_ASSERT(sizeof(EcPhase2AssetRequest)==120, "Phase2 asset request layout");
EC_STATIC_ASSERT(sizeof(EcPhase2PermissionRequest)==152, "Phase2 permission request layout");
EC_STATIC_ASSERT(sizeof(EcPhase2MutationRequest)==176, "Phase2 mutation layout");
EC_STATIC_ASSERT(sizeof(EcPhase2IdentitySnapshot)==104, "Phase2 identity layout");
EC_STATIC_ASSERT(sizeof(EcPhase2RoleSnapshot)==64, "Phase2 roles layout");
EC_STATIC_ASSERT(sizeof(EcPhase2AssetSnapshot)==56, "Phase2 asset snapshot layout");
EC_STATIC_ASSERT(sizeof(EcPhase2PermissionDecision)==64, "Phase2 permission decision layout");
EC_STATIC_ASSERT(sizeof(EcPhase2FeatureInfo)==64, "Phase2 feature layout");
EC_STATIC_ASSERT(sizeof(EcPhase2Submission)==72, "Phase2 submission layout");
EC_STATIC_ASSERT(sizeof(EcPhase2ReceiptRequest)==112, "Phase2 receipt request layout");
EC_STATIC_ASSERT(sizeof(EcPhase2Receipt)==176, "Phase2 receipt layout");
EC_STATIC_ASSERT(sizeof(EcPhase2OutboxRequest)==112, "Phase2 outbox request layout");
EC_STATIC_ASSERT(sizeof(EcPhase2OutboxEvent)==104, "Phase2 outbox event layout");
EC_STATIC_ASSERT(sizeof(EcPhase2OutboxBuffer)==48, "Phase2 outbox buffer layout");
EC_STATIC_ASSERT(sizeof(EcPhase2OutboxNotice)==32, "Phase2 outbox notice layout");
EC_STATIC_ASSERT(sizeof(EternalCorePhase2Api)==240, "Phase2 independent table layout");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,struct_size)==96, "Legacy prefix stays 96 bytes");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,get_phase2_features)==144, "Phase2 first method offset");
#ifdef __cplusplus
}
#endif
#endif
