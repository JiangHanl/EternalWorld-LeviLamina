#ifndef ETERNALSDK_ABI_H
#define ETERNALSDK_ABI_H

/* Windows x64, C11/C++17+, little endian, eight-byte aggregate packing.
 * This header is a contract, not a ninth runtime plugin or an authorization
 * implementation. See README.md for capability issuance and Phase 1 limits.
 */
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define EC_CALL __cdecl
#  if defined(ETERNAL_CORE_BUILD)
#    define EC_EXPORT __declspec(dllexport)
#  elif defined(ETERNAL_CORE_LINK_IMPORT)
#    define EC_EXPORT __declspec(dllimport)
#  else
#    define EC_EXPORT
#  endif
#else
#  define EC_CALL
#  define EC_EXPORT
#endif
#ifdef __cplusplus
#  define EC_NOEXCEPT noexcept
#  define EC_STATIC_ASSERT(condition, message) static_assert(condition, message)
extern "C" {
#else
#  define EC_NOEXCEPT
#  define EC_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#define EC_API_MAJOR UINT32_C(1)
#define EC_API_MINOR UINT32_C(0)
#define EC_STRUCT_VERSION UINT32_C(1)
#define EC_QUERY_API_EXPORT "EternalCore_QueryApi"
#define EC_MAX_REASON_UTF8_BYTES UINT32_C(256)
#define EC_COIN_MINOR_UNITS_PER_LIANG INT64_C(100)

typedef uint32_t EcStatus;
enum {
    EC_OK = 0,
    EC_INVALID_ARGUMENT = 1,
    EC_ABI_MISMATCH = 2,
    EC_UNSUPPORTED = 3,
    EC_NOT_READY = 4,
    EC_DENIED = 5,
    EC_EXPIRED = 6,
    EC_REVOKED = 7,
    EC_NOT_FOUND = 8,
    EC_CONFLICT = 9,
    EC_INSUFFICIENT_COIN = 10,
    EC_BUDGET_EXCEEDED = 11,
    EC_BUFFER_TOO_SMALL = 12,
    EC_STORAGE_FAILURE = 13,
    EC_PENDING = 14,
    EC_WRONG_THREAD = 15,
    EC_INVALID_UTF8 = 16,
    EC_LIMIT_EXCEEDED = 17,
    EC_INTERNAL_ERROR = 18
};

/* GetVersion/GetFeatures are mandatory and do not need feature bits. */
#define EC_FEATURE_CAPABILITY_CONTEXT (UINT64_C(1) << 0)
#define EC_FEATURE_READ_IDENTITY      (UINT64_C(1) << 1)
#define EC_FEATURE_READ_COIN          (UINT64_C(1) << 2)
#define EC_FEATURE_SUBMIT_TRANSFER    (UINT64_C(1) << 3)
#define EC_FEATURE_POLL_RECEIPT       (UINT64_C(1) << 4)
#define EC_PHASE1_FEATURES UINT64_C(0)

enum {
    EC_LIFECYCLE_STARTING = 0,
    EC_LIFECYCLE_READY = 1,
    EC_LIFECYCLE_DRAINING = 2,
    EC_LIFECYCLE_STOPPED = 3,
    EC_THREAD_DOMAIN_GAME_THREAD = 1,
    EC_IDENTITY_ONLINE = 1,
    EC_IDENTITY_OWNER = 2,
    EC_RECEIPT_QUEUED = 0,
    EC_RECEIPT_COMMITTED = 1,
    EC_RECEIPT_REJECTED = 2
};

#pragma pack(push, 8)

/* Opaque values, never pointers, timestamps, player IDs or self-authored grants. */
typedef struct EcId128 { uint64_t low; uint64_t high; } EcId128;
typedef EcId128 EcCapability;
typedef EcId128 EcIdempotencyKey;
typedef EcId128 EcReceiptId;
typedef struct EcUuid { uint8_t bytes[16]; } EcUuid;

/* Borrowed input valid only for this call. No embedded NUL; no implicit strlen. */
typedef struct EcUtf8View {
    const char* data;
    uint32_t length;
    uint32_t reserved;
} EcUtf8View;

/* Caller allocates/frees data. required includes the trailing NUL. No truncation.
 * Query size with data=NULL, capacity=0: returns EC_BUFFER_TOO_SMALL + required.
 */
typedef struct EcUtf8Buffer {
    char* data;
    uint32_t capacity;
    uint32_t required;
} EcUtf8Buffer;

typedef struct EcVersionInfo {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t core_major;
    uint32_t core_minor;
    uint32_t core_patch;
    uint32_t lifecycle;
    uint64_t instance_epoch;
    uint64_t reserved;
} EcVersionInfo;

typedef struct EcFeatureInfo {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t enabled;
    uint32_t domain_thread_model;
    uint32_t reserved0;
    uint64_t reserved1;
} EcFeatureInfo;

typedef struct EcIdentityRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcCapability capability;
    uint64_t subject_xuid;
    uint64_t expected_identity_revision; /* Zero means no revision precondition. */
    uint64_t reserved;
} EcIdentityRequest;

typedef struct EcIdentitySnapshot {
    uint32_t struct_size;
    uint32_t struct_version;
    EcUuid uuid; /* RFC 4122 canonical byte order, not an MC/C++ object layout. */
    uint64_t xuid;
    uint64_t role_mask; /* Informational snapshot, never a grant. */
    uint64_t identity_revision;
    uint64_t session_generation;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
} EcIdentitySnapshot;

typedef struct EcCoinRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcCapability capability;
    uint64_t subject_xuid;
    uint64_t reserved;
} EcCoinRequest;

typedef struct EcCoinSnapshot {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t subject_xuid;
    int64_t coin; /* Minor units: 100 per liang; nonnegative; limit belongs to Core. */
    uint64_t wallet_revision;
    uint64_t reserved;
} EcCoinSnapshot;

typedef struct EcTransferRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcCapability capability;
    EcIdempotencyKey idempotency_key;
    uint64_t from_xuid; /* Must match Core-authorized debit scope, not caller identity. */
    uint64_t to_xuid;
    int64_t gross_coin; /* Positive debit in minor units; no SDK/Core tax policy. */
    uint64_t expected_sender_revision;
    uint64_t expires_at_unix_ms;
    EcUtf8View reason;
    uint64_t reserved[2];
} EcTransferRequest;

typedef struct EcTransferSubmission {
    uint32_t struct_size;
    uint32_t struct_version;
    EcReceiptId receipt_id;
    uint32_t state;
    EcStatus result;
    uint64_t accepted_at_unix_ms;
    uint64_t reserved;
} EcTransferSubmission;

typedef struct EcReceiptRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcCapability capability;
    EcReceiptId receipt_id;
    uint64_t reserved;
} EcReceiptRequest;

typedef struct EcTransferReceipt {
    uint32_t struct_size;
    uint32_t struct_version;
    EcReceiptId receipt_id;
    uint32_t state;
    EcStatus result;
    uint64_t from_xuid;
    uint64_t to_xuid;
    int64_t gross_coin;
    int64_t fee_coin; /* Committed business-plan fee in minor units, not a Core tax rule. */
    uint64_t ledger_revision;
    uint64_t completed_at_unix_ms;
    uint64_t reserved;
} EcTransferReceipt;

typedef EcStatus (EC_CALL *EcGetVersionFn)(EcVersionInfo* out) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcGetFeaturesFn)(EcFeatureInfo* out) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadIdentityFn)(
    const EcIdentityRequest* request, EcIdentitySnapshot* out, EcUtf8Buffer* name
) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcReadCoinFn)(
    const EcCoinRequest* request, EcCoinSnapshot* out
) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcSubmitTransferFn)(
    const EcTransferRequest* request, EcTransferSubmission* out
) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcPollReceiptFn)(
    const EcReceiptRequest* request, EcTransferReceipt* out
) EC_NOEXCEPT;

/* Core-owned immutable table. Do not free it. No callback or private DB handle. */
typedef struct EternalCoreApi {
    uint32_t struct_size;
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t reserved0;
    EcGetVersionFn get_version;
    EcGetFeaturesFn get_features;
    EcReadIdentityFn read_identity;
    EcReadCoinFn read_coin;
    EcSubmitTransferFn submit_transfer;
    EcPollReceiptFn poll_receipt;
    uint64_t reserved[4];
} EternalCoreApi;

typedef const EternalCoreApi* (EC_CALL *EcQueryApiFn)(
    uint32_t api_major, uint32_t min_minor
) EC_NOEXCEPT;

EC_EXPORT const EternalCoreApi* EC_CALL EternalCore_QueryApi(
    uint32_t api_major, uint32_t min_minor
) EC_NOEXCEPT;

#pragma pack(pop)

EC_STATIC_ASSERT(sizeof(void*) == 8, "EternalSDK ABI requires a 64-bit process");
EC_STATIC_ASSERT(sizeof(EcId128) == 16, "EcId128 layout");
EC_STATIC_ASSERT(sizeof(EcUuid) == 16, "EcUuid layout");
EC_STATIC_ASSERT(sizeof(EcUtf8View) == 16, "EcUtf8View layout");
EC_STATIC_ASSERT(sizeof(EcUtf8Buffer) == 16, "EcUtf8Buffer layout");
EC_STATIC_ASSERT(sizeof(EcVersionInfo) == 48, "EcVersionInfo layout");
EC_STATIC_ASSERT(sizeof(EcFeatureInfo) == 32, "EcFeatureInfo layout");
EC_STATIC_ASSERT(sizeof(EcIdentityRequest) == 48, "EcIdentityRequest layout");
EC_STATIC_ASSERT(sizeof(EcIdentitySnapshot) == 72, "EcIdentitySnapshot layout");
EC_STATIC_ASSERT(sizeof(EcCoinRequest) == 40, "EcCoinRequest layout");
EC_STATIC_ASSERT(sizeof(EcCoinSnapshot) == 40, "EcCoinSnapshot layout");
EC_STATIC_ASSERT(sizeof(EcTransferRequest) == 112, "EcTransferRequest layout");
EC_STATIC_ASSERT(sizeof(EcTransferSubmission) == 48, "EcTransferSubmission layout");
EC_STATIC_ASSERT(sizeof(EcReceiptRequest) == 48, "EcReceiptRequest layout");
EC_STATIC_ASSERT(sizeof(EcTransferReceipt) == 88, "EcTransferReceipt layout");
EC_STATIC_ASSERT(sizeof(EternalCoreApi) == 96, "EternalCoreApi v1.0 layout");
EC_STATIC_ASSERT(offsetof(EcTransferRequest, reason) == 80, "Transfer reason offset");
EC_STATIC_ASSERT(offsetof(EternalCoreApi, get_version) == 16, "API table prefix");
EC_STATIC_ASSERT(offsetof(EternalCoreApi, poll_receipt) == 56, "API v1.0 final method");

#ifdef __cplusplus
} /* extern C */
#endif
#endif /* ETERNALSDK_ABI_H */
