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
EC_STATIC_ASSERT(sizeof(EternalCorePhase2Api)==EC_PHASE2_API_V1_2_SIZE, "1.2 table size");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,register_command_route)==240, "1.2 append-only route");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,unregister_command_route)==248, "1.2 removal offset");
EC_STATIC_ASSERT(offsetof(EternalCorePhase2Api,authorize_invocation)==256, "1.2 authorization offset");
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
