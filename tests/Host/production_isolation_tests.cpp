#include "EternalSDK/Core/phase2_abi.h"
#include "EternalSDK/Module/module_abi.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Image {
    HMODULE value{};
    explicit Image(const std::filesystem::path& path) : value(LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        require(value != nullptr, "Production DLL failed to load");
    }
    ~Image() { if (value) FreeLibrary(value); }
    template<class T> T symbol(const char* name) {
        const auto result = reinterpret_cast<T>(GetProcAddress(value, name));
        require(result != nullptr, "Required production export missing");
        return result;
    }
};
struct Scratch {
    std::filesystem::path path;
    Scratch() : path(std::filesystem::absolute("artifacts") / ("production-isolation-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(!std::filesystem::exists(path), "Scratch directory collision");
        std::filesystem::create_directories(path / "config");
        std::filesystem::create_directories(path / "data");
    }
    ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
};
struct Published { const EternalCorePhase2Api* api{}; };
EmStatus EM_CALL log(void*, uint32_t, EmUtf8View) noexcept { return EM_OK; }
EmStatus EM_CALL event(void*, const EmEvent*) noexcept { return EM_OK; }
EmStatus EM_CALL publish(void* instance, const EmServiceOffer* offer) noexcept {
    if (offer && std::string_view(offer->id.data, offer->id.length) == EC_PHASE2_SERVICE_ID) {
        if (offer->table_size != sizeof(EternalCorePhase2Api)) return EM_ABI_MISMATCH;
        static_cast<Published*>(instance)->api = static_cast<const EternalCorePhase2Api*>(offer->table);
    }
    return EM_OK;
}
EmUtf8View view(const std::string& text) { return {text.data(), static_cast<uint32_t>(text.size()), 0}; }
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Usage: ProductionIsolationTests <production Core DLL> <validation Core DLL>");
        Image image(std::filesystem::absolute(argv[1]));
        require(GetProcAddress(image.value, "EternalCore_ValidationBuildMarker") == nullptr,
            "Validation marker leaked into production DLL");
        require(GetProcAddress(image.value, "ll_mod_load") == nullptr,
            "Core module unexpectedly exports an LL entry");
        const auto load = image.symbol<EmLoadFn>(EM_LOAD_EXPORT);
        const auto enable = image.symbol<EmLifecycleFn>(EM_ENABLE_EXPORT);
        const auto disable = image.symbol<EmLifecycleFn>(EM_DISABLE_EXPORT);
        const auto unload = image.symbol<EmLifecycleFn>(EM_UNLOAD_EXPORT);
        for (unsigned flags = 0; flags != 4; ++flags) {
            Scratch scratch;
            {
                std::ofstream config(scratch.path / "config/core.json");
                config << "{\"ownerXuid\":\"1000\",\"developmentValidation\":"
                    << ((flags & 1) ? "true" : "false") << ",\"validatedAssets\":"
                    << ((flags & 2) ? "true" : "false") << ",\"moduleCapabilities\":{}}";
                require(static_cast<bool>(config), "Synthetic configuration write failed");
            }
            const auto configPath = (scratch.path / "config").string();
            const auto dataPath = (scratch.path / "data").string();
            Published published;
            EmHostContext context{sizeof(EmHostContext), EM_STRUCT_VERSION, EM_ABI_MAJOR, EM_ABI_MINOR,
                EM_HOST_CAPABILITIES, &published};
            context.config_directory = view(configPath);
            context.data_directory = view(dataPath);
            context.log = log;
            context.publish_service = publish;
            context.publish_event = event;
            const auto result = load(&context);
            if (flags) {
                require(result == EM_INTERNAL_ERROR, "Production accepted a validation configuration switch");
                require(enable() == EM_NOT_READY, "Rejected configuration left Core enabled");
            } else {
                require(result == EM_OK && enable() == EM_OK && published.api,
                    "Production default configuration did not enable");
                auto features = EcPhase2FeatureInfo{sizeof(EcPhase2FeatureInfo), EC_PHASE2_STRUCT_VERSION};
                auto legacy = EcFeatureInfo{sizeof(EcFeatureInfo), EC_STRUCT_VERSION};
                require(published.api->get_phase2_features(&features) == EC_OK
                    && features.enabled == 0 && features.development_only == 0,
                    "Production exposed development asset features");
                require(published.api->v1_0.get_features(&legacy) == EC_OK && legacy.enabled == 0,
                    "Production legacy asset features changed");
                require(disable() == EM_OK, "Production disable failed");
            }
            require(unload() == EM_OK, "Production cleanup failed");
        }
        // This identifies the separate binary; it grants no player or module authority.
        Image validation(std::filesystem::absolute(argv[2]));
        using Marker = uint32_t (EM_CALL *)() noexcept;
        require(validation.symbol<Marker>("EternalCore_ValidationBuildMarker")() == 1,
            "Separate validation binary marker missing");
        std::cout << "PASS ProductionIsolationTests: 5 groups; actual DLL configuration boundary, NO player validation\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
