#include "Host.hpp"
#include "EternalSDK/abi.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const EternalCoreApi* query(eternal::host::Host& host) {
    EmServiceRequest request{sizeof(EmServiceRequest), EM_STRUCT_VERSION,
        {EM_CORE_SERVICE_ID, sizeof(EM_CORE_SERVICE_ID)-1, 0}, EC_API_MAJOR, EC_API_MINOR, 0, 0};
    EmServiceReference result{sizeof(EmServiceReference), EM_STRUCT_VERSION};
    if (host.queryService(request, result) != EM_OK) return nullptr;
    require(result.table_size >= sizeof(EternalCoreApi), "Core service table too small");
    return static_cast<const EternalCoreApi*>(result.table);
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: ModuleArtifactTests <package Eternal directory>");
        const auto base = std::filesystem::absolute(argv[1]);
        const std::array<std::pair<const char*, const char*>, 8> modules{{
            {"core", "EternalCore"}, {"commerce", "EternalCommerce"}, {"life", "EternalLife"},
            {"world", "EternalWorld"}, {"content", "EternalContent"}, {"management", "EternalManagement"},
            {"presentation", "EternalPresentation"}, {"encounters", "EternalEncounters"},
        }};
        std::vector<eternal::host::ModuleConfig> configs;
        for (const auto& [id, name] : modules) {
            const auto relative = std::filesystem::path("modules") / (std::string(name) + ".dll");
            const auto image = LoadLibraryExW((base / relative).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            require(image != nullptr, "Actual module DLL failed to load");
            const auto descriptorFn = reinterpret_cast<EmGetDescriptorFn>(GetProcAddress(image, EM_GET_DESCRIPTOR_EXPORT));
            bool valid = descriptorFn != nullptr;
            for (const auto* symbol : {EM_LOAD_EXPORT, EM_ENABLE_EXPORT, EM_DISABLE_EXPORT, EM_UNLOAD_EXPORT})
                valid = valid && GetProcAddress(image, symbol) != nullptr;
            const auto* descriptor = descriptorFn ? descriptorFn() : nullptr;
            valid = valid && descriptor && descriptor->struct_size == sizeof(EmModuleDescriptor)
                && descriptor->struct_version == EM_STRUCT_VERSION && descriptor->abi_major == EM_ABI_MAJOR
                && descriptor->abi_minor == EM_ABI_MINOR
                && std::string_view(descriptor->id.data, descriptor->id.length) == id;
            if (descriptor) valid = valid && ((descriptor->flags & EM_MODULE_PLANNED) != 0) == (std::string_view(id) != "core");
            // Independent internal modules must not advertise LL NativeMod entry points.
            valid = valid && GetProcAddress(image, "ll_mod_load") == nullptr;
            FreeLibrary(image);
            require(valid, "Module descriptor/export contract failed");
            configs.push_back({id, relative, std::string_view(id) == "core", std::string_view(id) == "core"});
            std::cout << "PASS actual DLL contract " << name << '\n';
        }
        eternal::host::Host host;
        std::vector<eternal::host::Candidate> candidates;
        std::string error;
        if (!eternal::host::Host::discover(base, configs, candidates, error)) throw std::runtime_error(error);
        if (!host.load(candidates, error)) throw std::runtime_error(error);
        if (!host.enable(error)) throw std::runtime_error(error);
        const auto* api = query(host);
        require(api != nullptr, "Enabled Core service absent");
        EcVersionInfo version{sizeof(EcVersionInfo), EC_STRUCT_VERSION};
        EcFeatureInfo features{sizeof(EcFeatureInfo), EC_STRUCT_VERSION};
        require(api->get_version(&version) == EC_OK && version.lifecycle == EC_LIFECYCLE_READY, "Core lifecycle not ready");
        require(api->get_features(&features) == EC_OK && features.enabled == 0, "Unimplemented business capabilities advertised");
        if (!host.disable(error)) throw std::runtime_error(error);
        require(query(host) == nullptr, "Disabled provider service remains discoverable");
        // The previously discovered provider table is no longer usable while
        // disabled. Re-enable must obtain a fresh registry reference.
        if (!host.enable(error)) throw std::runtime_error(error);
        require(query(host) != nullptr, "Core service not republished after enable");
        if (!host.shutdown(error)) throw std::runtime_error(error);
        std::cout << "PASS actual DLL Host/Core load, service revocation, re-enable and unload\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
