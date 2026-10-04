/* Compile-only ABI test. This file is not a Core implementation or DLL. */
#pragma pack(push, 1)
#include "../include/EternalSDK/abi.h"
typedef struct CallerPacked { uint8_t tag; uint64_t value; } CallerPacked;
EC_STATIC_ASSERT(sizeof(CallerPacked) == 9, "SDK must restore caller packing");
#pragma pack(pop)
typedef struct CallerDefault { uint8_t tag; uint64_t value; } CallerDefault;
EC_STATIC_ASSERT(sizeof(CallerDefault) == 16, "SDK must not leak packing");

EC_STATIC_ASSERT(sizeof(EcStatus) == 4, "Status has fixed width");
EC_STATIC_ASSERT(EC_COIN_MINOR_UNITS_PER_LIANG == 100, "Currency uses minor units");
EC_STATIC_ASSERT(EC_PHASE1_FEATURES == 0, "Phase 1 must not advertise asset APIs");
EC_STATIC_ASSERT(offsetof(EcVersionInfo, instance_epoch) == 32, "Version epoch offset");
EC_STATIC_ASSERT(offsetof(EcFeatureInfo, enabled) == 8, "Features offset");
EC_STATIC_ASSERT(offsetof(EcIdentityRequest, capability) == 8, "Identity capability offset");
EC_STATIC_ASSERT(offsetof(EcIdentitySnapshot, xuid) == 24, "Identity XUID offset");
EC_STATIC_ASSERT(offsetof(EcIdentitySnapshot, flags) == 56, "Identity flags offset");
EC_STATIC_ASSERT(offsetof(EcCoinSnapshot, coin) == 16, "Coin offset");
EC_STATIC_ASSERT(offsetof(EcCoinSnapshot, wallet_revision) == 24, "Wallet revision offset");
EC_STATIC_ASSERT(offsetof(EcTransferRequest, idempotency_key) == 24, "Idempotency offset");
EC_STATIC_ASSERT(offsetof(EcTransferRequest, gross_coin) == 56, "Transfer amount offset");
EC_STATIC_ASSERT(offsetof(EcTransferSubmission, accepted_at_unix_ms) == 32, "Submission time offset");
EC_STATIC_ASSERT(offsetof(EcReceiptRequest, receipt_id) == 24, "Receipt query offset");
EC_STATIC_ASSERT(offsetof(EcTransferReceipt, ledger_revision) == 64, "Ledger offset");

#ifdef __cplusplus
EC_STATIC_ASSERT(__is_standard_layout(EternalCoreApi), "C ABI table is standard layout");
EC_STATIC_ASSERT(__is_trivially_copyable(EcTransferRequest), "Request has no ownership objects");
EC_STATIC_ASSERT(noexcept(((EcGetVersionFn)0)((EcVersionInfo*)0)), "No exception boundary");
#endif

/* These declarations verify pointer signatures in both languages without LL,
 * Windows SDK, imports, exception runtimes, allocators or a fake auth service.
 */
EcStatus EC_CALL probe_version(EcVersionInfo*) EC_NOEXCEPT;
EcStatus EC_CALL probe_features(EcFeatureInfo*) EC_NOEXCEPT;
EcStatus EC_CALL probe_identity(const EcIdentityRequest*, EcIdentitySnapshot*, EcUtf8Buffer*) EC_NOEXCEPT;
EcStatus EC_CALL probe_coin(const EcCoinRequest*, EcCoinSnapshot*) EC_NOEXCEPT;
EcStatus EC_CALL probe_transfer(const EcTransferRequest*, EcTransferSubmission*) EC_NOEXCEPT;
EcStatus EC_CALL probe_receipt(const EcReceiptRequest*, EcTransferReceipt*) EC_NOEXCEPT;
const EternalCoreApi* EC_CALL probe_query(uint32_t, uint32_t) EC_NOEXCEPT;

const EternalCoreApi signature_fixture = {
    sizeof(EternalCoreApi), EC_API_MAJOR, EC_API_MINOR, 0,
    probe_version, probe_features, probe_identity, probe_coin,
    probe_transfer, probe_receipt, {0, 0, 0, 0}
};
const EcQueryApiFn query_signature_fixture = probe_query;
int abi_probe_reference(void) {
    return signature_fixture.struct_size == 96 && query_signature_fixture != 0;
}
