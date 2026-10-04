#ifndef ETERNALSDK_MODULE_ABI_H
#define ETERNALSDK_MODULE_ABI_H

#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define EM_CALL __cdecl
# if defined(ETERNAL_MODULE_BUILD)
#  define EM_EXPORT __declspec(dllexport)
# else
#  define EM_EXPORT
# endif
#else
# define EM_CALL
# define EM_EXPORT
#endif
#ifdef __cplusplus
# define EM_NOEXCEPT noexcept
extern "C" {
#else
# define EM_NOEXCEPT
#endif

#define EM_ABI_MAJOR UINT32_C(1)
#define EM_ABI_MINOR UINT32_C(0)
#define EM_STRUCT_VERSION UINT32_C(1)
#define EM_CORE_MODULE_ID "core"
#define EM_CORE_SERVICE_ID "EternalCore.CoreApi"
#define EM_GET_DESCRIPTOR_EXPORT "EternalModule_GetDescriptor"
#define EM_LOAD_EXPORT "EternalModule_Load"
#define EM_ENABLE_EXPORT "EternalModule_Enable"
#define EM_DISABLE_EXPORT "EternalModule_Disable"
#define EM_UNLOAD_EXPORT "EternalModule_Unload"
#define EM_HOST_LOGGING (UINT64_C(1) << 0)
#define EM_HOST_SERVICES (UINT64_C(1) << 1)
#define EM_HOST_EVENTS (UINT64_C(1) << 2)
#define EM_HOST_CAPABILITIES (EM_HOST_LOGGING | EM_HOST_SERVICES | EM_HOST_EVENTS)
#define EM_MODULE_PLANNED UINT32_C(1)
#define EM_DEPENDENCY_OPTIONAL UINT32_C(1)

typedef uint32_t EmStatus;
enum { EM_OK=0, EM_INVALID_ARGUMENT=1, EM_ABI_MISMATCH=2,
       EM_NOT_READY=3, EM_NOT_FOUND=4, EM_CONFLICT=5, EM_UNSUPPORTED=6,
       EM_LIMIT_EXCEEDED=7, EM_INTERNAL_ERROR=8, EM_WRONG_THREAD=9 };
enum { EM_LOG_DEBUG=0, EM_LOG_INFO=1, EM_LOG_WARNING=2, EM_LOG_ERROR=3 };

#pragma pack(push, 8)
typedef struct EmUtf8View { const char* data; uint32_t length; uint32_t reserved; } EmUtf8View;
typedef struct EmSemVer { uint32_t major; uint32_t minor; uint32_t patch; uint32_t reserved; } EmSemVer;
typedef struct EmDependency {
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View module_id;
    EmSemVer minimum_version;
    uint64_t required_capabilities;
    uint32_t flags;
    uint32_t reserved;
} EmDependency;
typedef struct EmModuleDescriptor {
    /* Module-owned static data, including strings/dependency array, until its
     * image unloads. Host copies validated metadata before calling Load. */
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View id;
    EmUtf8View display_name;
    EmSemVer version;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t required_host_capabilities;
    uint64_t optional_host_capabilities;
    uint64_t provided_capabilities;
    const EmDependency* dependencies;
    uint32_t dependency_count;
    uint32_t flags;
    uint64_t reserved[2];
} EmModuleDescriptor;

/* Tables are provider-owned and valid only while that provider is enabled.
 * All service methods must independently reject work after Disable. No grants
 * arise from module IDs, descriptor capabilities or this discovery registry.
 */
typedef struct EmServiceOffer {
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View id;
    uint32_t api_major;
    uint32_t api_minor;
    uint64_t capabilities;
    const void* table;
    uint32_t table_size;
    uint32_t reserved;
} EmServiceOffer;
typedef struct EmServiceRequest {
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View id;
    uint32_t api_major;
    uint32_t minimum_minor;
    uint64_t required_capabilities;
    uint64_t reserved;
} EmServiceRequest;
typedef struct EmServiceReference {
    uint32_t struct_size;
    uint32_t struct_version;
    const void* table;
    uint32_t table_size;
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t reserved;
    uint64_t capabilities;
    uint64_t generation;
} EmServiceReference;
typedef struct EmEvent {
    /* topic/data are borrowed only for synchronous PublishEvent and callbacks.
     * A subscriber must copy its needed payload; never retain these pointers. */
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View topic;
    const void* data;
    uint32_t data_size;
    uint32_t payload_version;
    uint64_t reserved;
} EmEvent;
typedef void (EM_CALL *EmEventCallback)(void* user, const EmEvent* event) EM_NOEXCEPT;
typedef struct EmSubscription {
    uint32_t struct_size;
    uint32_t struct_version;
    EmUtf8View topic;
    EmEventCallback callback;
    void* user; /* Subscriber-owned until unsubscribe completes/Disable revokes.
                 Host never allocates, deletes or frees this value. */
    uint64_t reserved;
} EmSubscription;
typedef EmStatus (EM_CALL *EmLogFn)(void* instance, uint32_t level, EmUtf8View message) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmPublishServiceFn)(void* instance, const EmServiceOffer* offer) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmQueryServiceFn)(void* instance, const EmServiceRequest* request, EmServiceReference* out) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmSubscribeFn)(void* instance, const EmSubscription* subscription, uint64_t* token) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmPublishEventFn)(void* instance, const EmEvent* event) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmUnsubscribeFn)(void* instance, uint64_t token) EM_NOEXCEPT;
typedef struct EmHostContext {
    /* Load's struct pointer is borrowed. The module may copy its POD fields.
     * instance/path backing storage belong to Host for this binding, until
     * Unload completes. No callbacks or borrowed pointers may survive Unload.
     * Rejected/quarantined contexts fail closed; none is a player capability. */
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t capabilities;
    void* instance;
    EmUtf8View config_directory;
    EmUtf8View data_directory;
    EmUtf8View resources_directory;
    EmLogFn log;
    EmPublishServiceFn publish_service;
    EmQueryServiceFn query_service;
    EmSubscribeFn subscribe;
    EmPublishEventFn publish_event;
    EmUnsubscribeFn unsubscribe;
    uint64_t reserved[2];
} EmHostContext;
typedef const EmModuleDescriptor* (EM_CALL *EmGetDescriptorFn)(void) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmLoadFn)(const EmHostContext* context) EM_NOEXCEPT;
typedef EmStatus (EM_CALL *EmLifecycleFn)(void) EM_NOEXCEPT;

/* Load may run on the Host's startup thread. Before the first Enable attempt,
 * the trusted Host adapter may explicitly hand off once to the server thread,
 * only while no lifecycle/dispatch or service/subscription is active. Modules
 * must not assume Load and Enable share a thread. Enable and all later calls,
 * including context callbacks, stay on that bound thread; modules cannot rebind
 * it or call context callbacks from workers. Only terminal process shutdown may
 * run Disable/Unload on a shutdown thread, after Host's retained OS thread handle
 * proves the server thread exited; this permanently forbids Enable/Load again.
 * Terminal cleanup must not access a destroyed Bedrock world. This is not asset
 * authorization, and a Stopping flag alone never authorizes thread migration. */
EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor(void) EM_NOEXCEPT;
EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context) EM_NOEXCEPT;
EM_EXPORT EmStatus EM_CALL EternalModule_Enable(void) EM_NOEXCEPT;
EM_EXPORT EmStatus EM_CALL EternalModule_Disable(void) EM_NOEXCEPT;
EM_EXPORT EmStatus EM_CALL EternalModule_Unload(void) EM_NOEXCEPT;
#pragma pack(pop)

#ifdef __cplusplus
}
static_assert(sizeof(void*)==8, "Module ABI requires x64");
static_assert(sizeof(EmModuleDescriptor)==120, "Module descriptor layout");
static_assert(sizeof(EmHostContext)==144, "Host context layout");
#else
_Static_assert(sizeof(void*)==8, "Module ABI requires x64");
_Static_assert(sizeof(EmModuleDescriptor)==120, "Module descriptor layout");
_Static_assert(sizeof(EmHostContext)==144, "Host context layout");
#endif
#endif
