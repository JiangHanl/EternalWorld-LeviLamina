#pragma once
#include "EternalSDK/Core/native_ingress_abi.h"
#include "EternalSDK/Module/module_abi.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace eternal::core::runtime {
class Runtime final {
public:
    // Test-only C++ injection. Production uses steady_clock, never JSON wall time.
    using TestClock=std::function<std::uint64_t()>;
    Runtime(std::filesystem::path configDirectory,std::filesystem::path dataDirectory,TestClock clock={});
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    bool load();
    EmStatus enable(const EmHostContext&);
    // Terminal shutdown has no engine calls; Host must already prove quiescence.
    EmStatus disable() noexcept;
    const EternalCorePhase2Api* phase2Api()const noexcept;
    const EcNativeIngressApi* nativeApi()const noexcept;
    std::string diagnostics()const;
    const std::string& error()const noexcept;
#ifdef ETERNAL_CORE_RUNTIME_TESTING
    // Synthetic test binary only. Never defined in the production Core target.
    EcStatus testEnableFeatures(std::uint64_t mask);
    EcStatus testIssueCapability(const EcNativePlayerIdentity&,const EcPhase2MutationRequest& scope,EcCapability&);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
