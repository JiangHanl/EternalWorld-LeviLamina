#include "../include/EternalSDK/Module/module_abi.h"
#ifdef __cplusplus
#define CHECK(c) static_assert(c, #c)
#else
#define CHECK(c) _Static_assert(c, #c)
#endif
CHECK(sizeof(EmUtf8View)==16);
CHECK(sizeof(EmSemVer)==16);
CHECK(sizeof(EmDependency)==56);
CHECK(sizeof(EmModuleDescriptor)==120);
CHECK(sizeof(EmServiceOffer)==56);
CHECK(sizeof(EmServiceRequest)==48);
CHECK(sizeof(EmServiceReference)==48);
CHECK(sizeof(EmEvent)==48);
CHECK(sizeof(EmSubscription)==48);
CHECK(sizeof(EmHostContext)==144);
CHECK(offsetof(EmModuleDescriptor,optional_host_capabilities)==72);
CHECK(offsetof(EmHostContext,config_directory)==32);
CHECK(offsetof(EmHostContext,log)==80);
CHECK(offsetof(EmHostContext,unsubscribe)==120);
struct packing_restored { char c; uint64_t x; };
CHECK(sizeof(struct packing_restored)==16);
EmGetDescriptorFn descriptor_fn=EternalModule_GetDescriptor;
EmLoadFn load_fn=EternalModule_Load;
EmLifecycleFn enable_fn=EternalModule_Enable;
EmLifecycleFn disable_fn=EternalModule_Disable;
EmLifecycleFn unload_fn=EternalModule_Unload;
