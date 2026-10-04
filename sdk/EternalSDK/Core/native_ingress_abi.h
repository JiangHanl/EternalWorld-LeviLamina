#ifndef ETERNALSDK_NATIVE_INGRESS_ABI_H
#define ETERNALSDK_NATIVE_INGRESS_ABI_H

#include "phase2_abi.h"

/* TRUSTED HOST ADAPTER PROTOCOL, not a service callable by business modules.
 * Host MUST deny every module-origin query for core.native.* before lookup.
 * Only Host's native engine bridge may obtain this table and its secret nonce.
 * Core creates/rotates that nonce; a module ID, bool, pointer or self-authored
 * nonce cannot bootstrap it. Do not export an unauthenticated nonce/mint getter.
 * This is logical isolation for compliant DLLs, not a hostile native-DLL sandbox.
 * A registered plugin executes inside BDS and must itself be trusted.
 */
#define EC_NATIVE_INGRESS_SERVICE_ID "core.native.ingress"
#define EC_NATIVE_INGRESS_MAJOR UINT32_C(1)
#define EC_NATIVE_INGRESS_MINOR UINT32_C(0)
#define EC_NATIVE_INGRESS_STRUCT_VERSION UINT32_C(1)
#define EC_NATIVE_MAX_COMMAND_UTF8_BYTES UINT32_C(2048)
#define EC_NATIVE_MAX_REPLY_UTF8_BYTES UINT32_C(16384)
enum { EC_NATIVE_ORIGIN_PLAYER=1, EC_NATIVE_ORIGIN_SERVER_CONSOLE=2,
       EC_NATIVE_REPLY_SIMPLE_FORM=1, EC_NATIVE_REPLY_DEVELOPMENT_ONLY=2 };

#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 8)
typedef EcId128 EcNativeBridgeToken;
typedef EcId128 EcNativeSessionToken;
typedef EcId128 EcNativePendingAction;

/* Filled ONLY from authenticated engine Player/CommandOrigin/Form callback
 * objects by Host. Never copy identity/auth flags from command text, packets,
 * forms, a user-selected target, display names or claimed native OP status.
 * Core independently matches current UUID/XUID/session and configured Owner;
 * is_fully_authenticated must be 1 and is_simulated 0 for real player authority.
 */
typedef struct EcNativePlayerIdentity {
    uint32_t struct_size;
    uint32_t struct_version;
    EcUuid trusted_uuid;
    EcUuid client_uuid;
    uint64_t trusted_xuid;
    EcUtf8View display_name;
    uint32_t is_fully_authenticated;
    uint32_t is_simulated;
} EcNativePlayerIdentity;

typedef struct EcNativeJoinResult {
    uint32_t struct_size;
    uint32_t struct_version;
    EcPlayerId player_id;
    EcNativeSessionToken session_token;
    uint64_t identity_revision;
    uint64_t role_revision;
    uint64_t session_generation;
    uint32_t created;
    uint32_t reserved0;
    uint64_t reserved1;
} EcNativeJoinResult;

typedef struct EcNativeDisconnectRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcUuid client_uuid;
    uint64_t expected_session_generation;
    uint64_t reserved;
} EcNativeDisconnectRequest;

/* module_id is authenticated Host descriptor metadata here, never a public
 * caller assertion. generation comes from Host Node Enable, strictly increasing.
 * Host approval/budget fields are only upper bounds, not an authorization source.
 * Core intersects them with its independent private module policy, default deny. Unknown
 * bits, missing Core, unapproved capabilities or duplicate generations refuse.
 * A zero limit/budget permits no corresponding asset write; no unlimited magic.
 */
typedef struct EcNativeModuleBindingRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcUtf8View module_id;
    uint64_t module_generation;
    uint64_t approved_module_capabilities;
    uint64_t approved_permissions;
    int64_t money_limit_per_request;
    int64_t reputation_limit_per_request;
    int64_t money_budget_per_period;
    int64_t reputation_budget_per_period;
    uint64_t budget_period_ms;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
} EcNativeModuleBindingRequest;

typedef struct EcNativeModuleBindingResult {
    uint32_t struct_size;
    uint32_t struct_version;
    const EternalCorePhase2Api* scoped_api; /* Core-owned until revoke/Disable. */
    uint32_t table_size;
    uint32_t reserved0;
    EcCallerContext caller_context;
    uint64_t caller_generation;
    uint64_t instance_epoch;
    uint64_t reserved1;
} EcNativeModuleBindingResult;

typedef struct EcNativeModuleRevokeRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcCallerContext caller_context;
    uint64_t caller_generation;
    EcUtf8View reason;
    uint64_t reserved;
} EcNativeModuleRevokeRequest;

typedef struct EcNativeCommandRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcRequestId request_id;
    uint32_t origin;
    uint32_t flags;
    EcNativePlayerIdentity player; /* All zero for genuine server console. */
    EcUtf8View command_text;
    uint64_t reserved;
} EcNativeCommandRequest;

typedef struct EcNativeActionCompletionRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EcRequestId request_id;
    EcNativePlayerIdentity player; /* Re-extracted from callback Player. */
    EcNativePendingAction pending_action;
    int32_t selected_button; /* -1 = cancelled; never a command string. */
    uint32_t reserved0;
    uint64_t reserved1;
} EcNativeActionCompletionRequest;

typedef struct EcNativeReply {
    uint32_t struct_size;
    uint32_t struct_version;
    EcStatus result;
    uint32_t flags;
    EcNativePendingAction pending_action;
    uint64_t reserved;
} EcNativeReply;

/* Caller-owned UTF8 buffer contains plain diagnostic text or a Core-generated
 * native SimpleForm JSON, as indicated by flags. Host only validates/renders the
 * generic form; Core owns buttons and permission/asset decisions. A pending
 * action stores Core-issued capability, identity/session/role/caller versions
 * and the exact action scope. Click/old UI rechecks every binding and consumes
 * or cancels that token; it never authorizes by supplied form text.
 * request_id is nonzero and reply retries must not repeat a mutation or mint a
 * different pending action. Core retains an identical bounded reply for the same
 * native request; changed origin/player/command/button with that ID conflicts.
 * Synthetic/development console validation is explicit and separately gated;
 * ordinary module API assets stay disabled and do not fallback to that channel.
 */
typedef EcStatus (EC_CALL *EcNativeBindModuleFn)(EcNativeBridgeToken, const EcNativeModuleBindingRequest*, EcNativeModuleBindingResult*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeRevokeModuleFn)(EcNativeBridgeToken, const EcNativeModuleRevokeRequest*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeAuthenticatedPlayerFn)(EcNativeBridgeToken, const EcNativePlayerIdentity*, EcNativeJoinResult*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeDisconnectFn)(EcNativeBridgeToken, const EcNativeDisconnectRequest*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeCommandFn)(EcNativeBridgeToken, const EcNativeCommandRequest*, EcNativeReply*, EcUtf8Buffer*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeCompleteActionFn)(EcNativeBridgeToken, const EcNativeActionCompletionRequest*, EcNativeReply*, EcUtf8Buffer*) EC_NOEXCEPT;
typedef EcStatus (EC_CALL *EcNativeTickFn)(EcNativeBridgeToken, uint64_t unix_millis) EC_NOEXCEPT;

typedef struct EcNativeIngressApi {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t api_major;
    uint32_t api_minor;
    EcNativeBridgeToken bridge_nonce;
    uint64_t instance_epoch;
    EcNativeBindModuleFn bind_module;
    EcNativeRevokeModuleFn revoke_module;
    EcNativeAuthenticatedPlayerFn authenticated_player;
    EcNativeDisconnectFn disconnect;
    EcNativeCommandFn native_command;
    EcNativeCompleteActionFn complete_action;
    EcNativeTickFn tick;
    uint64_t reserved[4];
} EcNativeIngressApi;

/* Game-thread protocol only, with the same terminal-shutdown exception as the
 * Module ABI. Disable revokes native/player/module tokens and pending actions.
 * New Enable rotates nonce/epoch bindings; stale tables must be discarded.
 * Host tick time is advisory; it cannot extend expired capability lifetime or
 * roll Core's authorization clock backwards. Buffers/views have ordinary Core
 * ABI ownership: input is call-borrowed, output storage belongs to caller.
 */
#pragma pack(pop)
EC_STATIC_ASSERT(sizeof(EcNativePlayerIdentity)==72, "Native player identity layout");
EC_STATIC_ASSERT(sizeof(EcNativeJoinResult)==80, "Native join result layout");
EC_STATIC_ASSERT(sizeof(EcNativeDisconnectRequest)==40, "Native disconnect layout");
EC_STATIC_ASSERT(sizeof(EcNativeModuleBindingRequest)==104, "Native binding request layout");
EC_STATIC_ASSERT(sizeof(EcNativeModuleBindingResult)==64, "Native binding result layout");
EC_STATIC_ASSERT(sizeof(EcNativeModuleRevokeRequest)==56, "Native revoke request layout");
EC_STATIC_ASSERT(sizeof(EcNativeCommandRequest)==128, "Native command request layout");
EC_STATIC_ASSERT(sizeof(EcNativeActionCompletionRequest)==128, "Native action completion layout");
EC_STATIC_ASSERT(sizeof(EcNativeReply)==40, "Native reply layout");
EC_STATIC_ASSERT(sizeof(EcNativeIngressApi)==128, "Native ingress table layout");
EC_STATIC_ASSERT(offsetof(EcNativeIngressApi,bind_module)==40, "Native first method offset");
#ifdef __cplusplus
}
#endif
#endif
