#include "EternalSDK/Module/module_abi.h"

namespace {
const EmModuleDescriptor descriptor{
    .struct_size = sizeof(EmModuleDescriptor),
    .struct_version = EM_STRUCT_VERSION,
    .id = {"management", 10, 0},
    .display_name = {"EternalManagement", 17, 0},
    .version = {0, 1, 0, 0},
    .abi_major = EM_ABI_MAJOR,
    .abi_minor = EM_ABI_MINOR,
    .required_host_capabilities = 0,
    .provided_capabilities = 0,
    .dependencies = nullptr,
    .dependency_count = 0,
    .flags = EM_MODULE_PLANNED,
};
}
extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor() noexcept { return &descriptor; }
// PLANNED / NOT IMPLEMENTED. No fake business service, UI or database.
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext*) noexcept { return EM_UNSUPPORTED; }
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept { return EM_UNSUPPORTED; }
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept { return EM_OK; }
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept { return EM_OK; }
