#pragma pack(push, 1)
#include "../Core/phase2_abi.h"
struct Phase2RestoredPacking { char byte; uint64_t value; };
EC_STATIC_ASSERT(offsetof(struct Phase2RestoredPacking,value)==1, "Packing restored");
#pragma pack(pop)
EC_STATIC_ASSERT(sizeof(EternalCoreApi)==96, "Legacy ABI unchanged");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,v1_0)==0, "Legacy prefix at zero");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,caller_context)==112, "Caller context offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,read_outbox)==200, "Last Phase2 method offset");
EC_STATIC_ASSERT(sizeof(EternalCorePhase2ApiV1_1)==EC_PHASE2_API_V1_1_SIZE, "1.1 prefix preserved");
EC_STATIC_ASSERT(sizeof(EternalCorePhase2ApiV1_2)==EC_PHASE2_API_V1_2_SIZE, "1.2 table preserved");
EC_STATIC_ASSERT(sizeof(EternalCorePhase2Api)==EC_PHASE2_API_V1_3_SIZE, "1.3 table size");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,register_command_route)==240, "1.2 append-only route");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,unregister_command_route)==248, "1.2 removal offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,authorize_invocation)==256, "1.2 authorization offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,register_consumer)==264, "1.3 registration offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,query_consumer)==272, "1.3 query offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,ack_consumer_event)==280, "1.3 ACK offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,retry_consumer_event)==288, "1.3 retry offset");
#define EC_V12_PREFIX(field) EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,field)==offsetof(EternalCorePhase2ApiV1_2,field), "1.2 prefix field " #field)
EC_V12_PREFIX(v1_0);EC_V12_PREFIX(struct_size);EC_V12_PREFIX(struct_version);EC_V12_PREFIX(api_major);EC_V12_PREFIX(api_minor);
EC_V12_PREFIX(caller_context);EC_V12_PREFIX(caller_generation);EC_V12_PREFIX(instance_epoch);EC_V12_PREFIX(get_phase2_features);
EC_V12_PREFIX(read_identity_v2);EC_V12_PREFIX(read_roles);EC_V12_PREFIX(check_permission);EC_V12_PREFIX(read_asset);
EC_V12_PREFIX(submit_mutation);EC_V12_PREFIX(poll_receipt_v2);EC_V12_PREFIX(read_outbox);EC_V12_PREFIX(reserved);
EC_V12_PREFIX(register_command_route);EC_V12_PREFIX(unregister_command_route);EC_V12_PREFIX(authorize_invocation);
#undef EC_V12_PREFIX
EC_STATIC_ASSERT(sizeof(EcPhase2Invocation)==152&&offsetof(EcPhase2Invocation,arguments)==120, "Invocation POD");
EC_STATIC_ASSERT(sizeof(EcPhase2CommandRouteRequest)==72&&sizeof(EcPhase2RouteRemovalRequest)==48, "Route POD");
EC_STATIC_ASSERT(sizeof(EcPhase2InvocationAuthorization)==120&&sizeof(EcPhase2InvocationGrant)==80, "Grant POD");
EC_STATIC_ASSERT(offsetof(EcPhase2RequestMeta,capability)==24, "Capability offset");
EC_STATIC_ASSERT(offsetof(EcPhase2RequestMeta,expected_revision)==72, "Revision offset");
EC_STATIC_ASSERT(offsetof(EcPhase2MutationRequest,reason)==144, "Reason offset");
EC_STATIC_ASSERT(sizeof(EcReadPhase2IdentityFn)==8, "Function pointer size");
EC_STATIC_ASSERT((EC_P2_ROLE_MUTABLE_ALL&EC_P2_ROLE_OWNER)==0, "Owner immutable");
EC_STATIC_ASSERT((EC_P2_ROLE_MUTABLE_ALL&EC_P2_ROLE_PLAYER)==0, "Player implicit");
EC_STATIC_ASSERT(EC_PHASE1_FEATURES==0&&EC_PHASE2_PRODUCTION_FEATURES==0, "Production gates unchanged");
int phase2_layout_probe(void) {
#ifdef __cplusplus
    EcPhase2MutationRequest request{};
#else
    EcPhase2MutationRequest request={0};
#endif
    request.meta.struct_size=sizeof(request);
    request.meta.struct_version=EC_PHASE2_STRUCT_VERSION;
    return request.meta.struct_size==176&&request.meta.struct_version==1?0:1;
}
