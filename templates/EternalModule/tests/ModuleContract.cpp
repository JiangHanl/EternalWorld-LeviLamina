#include "EternalSDK/Module/module_abi.h"
#include "EternalSDK/abi.h"
#include <string_view>

namespace {
int versionCalls = 0;
EcStatus EC_CALL version(EcVersionInfo*) noexcept { ++versionCalls; return EC_OK; }
EternalCoreApi table{};
EmStatus EM_CALL log(void*, uint32_t, EmUtf8View) noexcept { return EM_OK; }
EmStatus EM_CALL subscribe(void*, const EmSubscription*, uint64_t* token) noexcept { *token=1; return EM_OK; }
EmStatus EM_CALL unsubscribe(void*, uint64_t) noexcept { return EM_OK; }
EmStatus EM_CALL query(void*, const EmServiceRequest*, EmServiceReference* out) noexcept {
    *out = {sizeof(*out), EM_STRUCT_VERSION, &table, sizeof(table), EC_API_MAJOR, EC_API_MINOR, 0, 0, 1};
    return EM_OK;
}
}

int main() {
    const auto* descriptor = EternalModule_GetDescriptor();
    if (!descriptor || descriptor->struct_size != sizeof(EmModuleDescriptor)) return 1;
    if (descriptor->struct_version != EM_STRUCT_VERSION || descriptor->abi_major != EM_ABI_MAJOR) return 2;
    if (std::string_view(descriptor->id.data, descriptor->id.length) != "example") return 3;
    if (EternalModule_Load(nullptr) != EM_ABI_MISMATCH) return 4;
    if (EternalModule_Enable() != EM_NOT_READY) return 5;
    if (EternalModule_Disable() != EM_OK || EternalModule_Unload() != EM_OK) return 6;
    EmHostContext host{};
    host.struct_size=sizeof(host); host.struct_version=EM_STRUCT_VERSION;
    host.abi_major=EM_ABI_MAJOR; host.abi_minor=EM_ABI_MINOR;
    host.capabilities=EM_HOST_CAPABILITIES; host.instance=&host;
    host.config_directory={".",1,0}; host.log=log; host.subscribe=subscribe;
    host.unsubscribe=unsubscribe; host.query_service=query;
    table.api_major=EC_API_MAJOR; table.get_version=version;
    if (EternalModule_Load(&host)!=EM_OK || EternalModule_Enable()!=EM_OK || versionCalls!=0) return 7;
    if (EternalModule_Disable()!=EM_OK) return 8;
    table.struct_size=sizeof(table); table.api_major=EC_API_MAJOR+1;
    if (EternalModule_Enable()!=EM_OK || versionCalls!=0 || EternalModule_Disable()!=EM_OK) return 9;
    table.api_major=EC_API_MAJOR;
    if (EternalModule_Enable()!=EM_OK || versionCalls!=1) return 10;
    if (EternalModule_Disable()!=EM_OK || EternalModule_Unload()!=EM_OK) return 11;
}
