#pragma pack(push, 1)
#include "../Core/native_ingress_abi.h"
struct NativeRestoredPacking { char byte; uint64_t value; };
EC_STATIC_ASSERT(offsetof(struct NativeRestoredPacking,value)==1, "Native packing restored");
#pragma pack(pop)
EC_STATIC_ASSERT(offsetof(EcNativePlayerIdentity,trusted_xuid)==40, "Authenticated XUID offset");
EC_STATIC_ASSERT(offsetof(EcNativePlayerIdentity,is_fully_authenticated)==64, "Authentication flag offset");
EC_STATIC_ASSERT(offsetof(EcNativeCommandRequest,player)==32, "Native command player offset");
EC_STATIC_ASSERT(offsetof(EcNativeCommandRequest,command_text)==104, "Native command text offset");
EC_STATIC_ASSERT(offsetof(EcNativeActionCompletionRequest,pending_action)==96, "Action token offset");
EC_STATIC_ASSERT(offsetof(EcNativeIngressApi,complete_action)==80, "Native complete method offset");
EC_STATIC_ASSERT(sizeof(EcNativeBindModuleFn)==8, "Native function pointer size");
int native_ingress_layout_probe(void) {
#ifdef __cplusplus
    EcNativeModuleBindingRequest request{};
#else
    EcNativeModuleBindingRequest request={0};
#endif
    request.struct_size=sizeof(request);
    request.struct_version=EC_NATIVE_INGRESS_STRUCT_VERSION;
    return request.struct_size==104?0:1;
}
