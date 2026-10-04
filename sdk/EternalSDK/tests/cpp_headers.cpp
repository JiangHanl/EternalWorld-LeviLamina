#include "EternalSDK/abi/core_abi.h"
#include "EternalSDK/abi/module_abi.h"
#include "EternalSDK/Module/Module.hpp"
#include "EternalSDK/Core/Client.hpp"
#include "EternalSDK/Events/Events.hpp"
#include "EternalSDK/UI/UI.hpp"
#include "EternalSDK/Types/Types.hpp"
#include "EternalSDK/DTO/Core.hpp"
#include "EternalSDK/Version/Version.hpp"
#include "EternalSDK/cpp/ModuleExports.hpp"
#include <iostream>
#include <stdexcept>
namespace {
struct Module {
    static const EmModuleDescriptor* descriptor(){static const EmModuleDescriptor d{};return &d;}
    EmStatus load(const EmHostContext&){throw std::runtime_error("local module failure");}
    EmStatus enable(){return EM_OK;}
    EmStatus disable(){return EM_OK;}
    EmStatus unload(){return EM_OK;}
};
}
EM_IMPLEMENT_MODULE(Module)
int main() {
    EmHostContext host{};host.struct_size=sizeof(host);host.struct_version=EM_STRUCT_VERSION;
    host.abi_major=EM_ABI_MAJOR;host.abi_minor=EM_ABI_MINOR;host.instance=&host;
    const std::string own="module-config";host.config_directory=eternal::sdk::view(own);
    eternal::sdk::ModuleContext context(host);
    if(context.configFile("settings.json").filename()!="settings.json")return 1;
    for(auto bad:{"../config.json","a/b.json","a\\b.json","C:config.json",".."}) {
        try {context.configFile(bad);return 2;}catch(const std::invalid_argument&){}
    }
    if(EternalModule_Load(nullptr)!=EM_INVALID_ARGUMENT||EternalModule_Load(&host)!=EM_INTERNAL_ERROR)return 3;
    if(EternalModule_Enable()!=EM_OK||EternalModule_Disable()!=EM_OK||EternalModule_Unload()!=EM_OK)return 4;
    static_assert(!eternal::sdk::ui::serviceImplemented);
    static_assert(eternal::sdk::minorUnitsPerLiang==100);
    std::cout<<"PASS: SDK C++ headers, private config paths and module-local exception boundary\n";
}
