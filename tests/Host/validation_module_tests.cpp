#include "Host.hpp"
#include "EternalSDK/Core/phase2_abi.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Selfcheck = EmStatus (EM_CALL *)(EcPhase2FeatureInfo*, EcStatus*, uint32_t*) noexcept;
struct Image {
    HMODULE value{};
    explicit Image(const std::filesystem::path& path) : value(LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        require(value != nullptr, "Fixture DLL failed to load");
    }
    ~Image() { if (value) FreeLibrary(value); }
    template<class T> T symbol(const char* name) {
        const auto result = reinterpret_cast<T>(GetProcAddress(value, name));
        require(result != nullptr, "Fixture export missing");
        return result;
    }
};
struct Scratch {
    std::filesystem::path path;
    Scratch() : path(std::filesystem::absolute("artifacts") / ("validation-module-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(!std::filesystem::exists(path), "Scratch directory collision");
        std::filesystem::create_directories(path / "modules");
    }
    ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
};
EmStatus EM_CALL log(void*, uint32_t, EmUtf8View) noexcept { return EM_OK; }
EmStatus EM_CALL oldService(void*, const EmServiceRequest*, EmServiceReference* result) noexcept {
    static const uint64_t placeholder{};
    *result = {sizeof(*result), EM_STRUCT_VERSION, &placeholder, 240, 1, 1, 0, EC_MODULE_CAP_ALL, 1};
    return EM_OK;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Usage: ValidationModuleTests <Eternal directory> <fixture DLL>");
        const auto base = std::filesystem::absolute(argv[1]);
        const auto fixture = std::filesystem::absolute(argv[2]);
        Scratch scratch;
        const auto imagePath = scratch.path / "modules/CoreValidationModule.dll";
        std::filesystem::copy_file(fixture, imagePath);
        Image image(imagePath);
        const auto descriptor = image.symbol<EmGetDescriptorFn>(EM_GET_DESCRIPTOR_EXPORT)();
        require(descriptor && descriptor->provided_capabilities == 0
            && descriptor->dependency_count == 1
            && descriptor->dependencies[0].required_capabilities == EC_MODULE_CAP_ALL,
            "Fixture declared capability/dependency contract failed");
        require(GetProcAddress(image.value, "ll_mod_load") == nullptr, "Fixture must not depend on LL entry points");
        const auto load = image.symbol<EmLoadFn>(EM_LOAD_EXPORT);
        const auto enable = image.symbol<EmLifecycleFn>(EM_ENABLE_EXPORT);
        const auto unload = image.symbol<EmLifecycleFn>(EM_UNLOAD_EXPORT);
        const auto selfcheck = image.symbol<Selfcheck>("CoreValidationModule_Selfcheck");
        int instance{};
        EmHostContext oldHost{sizeof(EmHostContext), EM_STRUCT_VERSION, EM_ABI_MAJOR, EM_ABI_MINOR,
            EM_HOST_CAPABILITIES, &instance};
        oldHost.log = log;
        oldHost.query_service = oldService;
        require(load(&oldHost) == EM_OK && enable() == EM_ABI_MISMATCH && unload() == EM_OK,
            "Actual fixture DLL accepted an API 1.1 service");
        std::cout << "PASS actual fixture DLL rejects API 1.1 contract\n";

        std::string error;
        {
            eternal::host::Host missing;
            require(!missing.load({{"core", scratch.path / "modules/missing-core.dll", true, true},
                {"core-validation", imagePath, true, true}}, error), "Missing required Core accepted");
        }
        std::cout << "PASS actual Host rejects missing required Core\n";
        std::filesystem::copy_file(base / "modules/EternalCore.dll", scratch.path / "modules/EternalCore.dll");
        std::filesystem::create_directories(scratch.path / "config/core");
        {
            std::ofstream config(scratch.path / "config/core/core.json");
            config << R"({"ownerXuid":"1000","developmentValidation":false,"validatedAssets":false,"moduleCapabilities":{"core-validation":["core.player.read.v1","core.permission.check.v1","core.transaction.money.v1","core.transaction.reputation.v1","core.audit.write.v1","core.events.v1"]}})";
            require(static_cast<bool>(config), "Synthetic test configuration write failed");
        }
        eternal::host::Host host;
        std::vector<eternal::host::Candidate> candidates;
        require(eternal::host::Host::discover(scratch.path,
            {{"core", "modules/EternalCore.dll", true, true},
             {"core-validation", "modules/CoreValidationModule.dll", true, true}}, candidates, error),
            "Actual Core/fixture discovery failed");
        if (!host.load(candidates, error) || !host.enable(error)) throw std::runtime_error(error);
        auto features = EcPhase2FeatureInfo{sizeof(EcPhase2FeatureInfo), EC_PHASE2_STRUCT_VERSION};
        EcStatus readStatus{};
        uint32_t registered{};
        require(selfcheck(&features, &readStatus, &registered) == EM_OK
            && features.enabled == 0 && registered == 1 && readStatus == EC_UNSUPPORTED,
            "Actual scoped route/default production feature boundary failed");
        std::cout << "PASS actual Core+Host+fixture scoped API 1.3 route; production features 0/Unsupported\n";
        if (!host.disable(error)) throw std::runtime_error(error);
        require(selfcheck(&features, &readStatus, &registered) == EM_NOT_READY,
            "Disabled fixture still exposes active client/route state");
        if (!host.enable(error)) throw std::runtime_error(error);
        require(selfcheck(&features, &readStatus, &registered) == EM_OK && registered == 1,
            "Fixture route failed to rediscover/register after re-enable");
        if (!host.shutdown(error)) throw std::runtime_error(error);
        std::cout << "PASS actual fixture disable, route revocation, re-enable and unload\n";
        std::cout << "PASS ValidationModuleTests: 4 groups; NO authenticated-player or asset-mutation validation\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
