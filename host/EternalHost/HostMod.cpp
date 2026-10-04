#include "HostMod.hpp"
#include "runtime/Config.hpp"
#include "EternalSDK/abi.h"

#define LL_MEMORY_OPERATORS
#include "ll/api/memory/MemoryOperators.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/GamingStatus.h"
#include "mc/server/ServerInstance.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"

namespace {
const EternalCoreApi* coreApi(eternal::host::Host& host) {
    const EmServiceRequest request{
        sizeof(EmServiceRequest), EM_STRUCT_VERSION,
        {EM_CORE_SERVICE_ID, sizeof(EM_CORE_SERVICE_ID)-1, 0}, EC_API_MAJOR, EC_API_MINOR, 0, 0,
    };
    EmServiceReference reference{};
    reference.struct_size = sizeof(reference);
    reference.struct_version = EM_STRUCT_VERSION;
    if (host.queryService(request, reference) != EM_OK || reference.table_size < sizeof(EternalCoreApi)) return nullptr;
    const auto* api = static_cast<const EternalCoreApi*>(reference.table);
    return api && api->struct_size >= sizeof(EternalCoreApi) && api->api_major == EC_API_MAJOR
        && api->api_minor >= EC_API_MINOR ? api : nullptr;
}
bool healthy(const EternalCoreApi* api) {
    if (!api || !api->get_version || !api->get_features || !api->read_identity || !api->read_coin
        || !api->submit_transfer || !api->poll_receipt) return false;
    EcVersionInfo version{sizeof(EcVersionInfo), EC_STRUCT_VERSION};
    EcFeatureInfo features{sizeof(EcFeatureInfo), EC_STRUCT_VERSION};
    return api->get_version(&version) == EC_OK && version.lifecycle == EC_LIFECYCLE_READY
        && api->get_version(nullptr) == EC_INVALID_ARGUMENT
        && api->get_features(&features) == EC_OK && features.enabled == 0
        && api->read_identity(nullptr, nullptr, nullptr) == EC_UNSUPPORTED
        && api->read_coin(nullptr, nullptr) == EC_UNSUPPORTED
        && api->submit_transfer(nullptr, nullptr) == EC_UNSUPPORTED
        && api->poll_receipt(nullptr, nullptr) == EC_UNSUPPORTED;
}
}

namespace eternal::adapter {
HostMod::HostMod() : self_(*ll::mod::NativeMod::current()), host_({}, [this](uint32_t level, std::string_view message) {
    if (level == EM_LOG_ERROR) self_.getLogger().error("{}", message);
    else if (level == EM_LOG_WARNING) self_.getLogger().warn("{}", message);
    else self_.getLogger().info("{}", message);
}) {}

HostMod& HostMod::getInstance() {
    static HostMod instance;
    return instance;
}

bool HostMod::load() {
    try {
        const auto base = self_.getModDir();
        std::vector<eternal::host::ModuleConfig> config;
        std::vector<eternal::host::Candidate> candidates;
        std::string error;
        if (!eternal::host::loadConfig(base / "config/modules.json", config, error)
            || !eternal::host::Host::discover(base, config, candidates, error)
            || !host_.load(candidates, error)) {
            self_.getLogger().error("EternalHost safely refused startup: {}", error);
            if (host_.snapshot().quarantined) {
                // A failed module may still hold callback addresses into Host.
                // Keep this LL image resident, while runtime.enable refuses work.
                self_.getLogger().error("Quarantined module retained; restart is required before retrying");
                return true;
            }
            return false;
        }
        self_.getLogger().info("EternalHost loaded | module ABI 1.0 | API 1.0 draft");
        return true;
    } catch (const std::exception& error) {
        self_.getLogger().error("EternalHost load failed: {}", error.what());
        return host_.snapshot().quarantined;
    } catch (...) {
        self_.getLogger().error("EternalHost load failed at the native adapter boundary");
        return host_.snapshot().quarantined;
    }
}

bool HostMod::enable() {
    try {
        std::string error;
        if (!firstEnableThreadBound_) {
            if (!host_.bindFirstEnableThread(error)) {
                self_.getLogger().error("EternalHost enable thread handoff failed: {}", error);
                return false;
            }
            firstEnableThreadBound_ = true;
        }
        if (!host_.enable(error)) {
            self_.getLogger().error("EternalHost enable failed: {}", error);
            return false;
        }
        if (!native_) native_ = std::make_unique<LLAdapter>(host_, [this](uint32_t level, std::string_view message) {
            if (level == EM_LOG_ERROR) self_.getLogger().error("{}", message);
            else if (level == EM_LOG_WARNING) self_.getLogger().warn("{}", message);
            else self_.getLogger().info("{}", message);
        });
        if (!native_->enable()) {
            host_.disable(error);
            self_.getLogger().error("EternalHost authenticated native ingress registration failed");
            return false;
        }
        auto& command = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
            "ecore", "永恒核心：服务健康检查", CommandPermissionLevel::Any);
        command.overload().text("status").execute([this](CommandOrigin const&, CommandOutput& output) {
            if (!healthy(coreApi(host_))) { output.error("EternalCore is not ready"); return; }
            output.success("EternalCore 0.1.0 | API 1.0 | internal module lifecycle ready | business features disabled");
        });
        command.overload().text("selfcheck").execute([this](CommandOrigin const&, CommandOutput& output) {
            if (healthy(coreApi(host_))) output.success("EternalCore ABI selfcheck PASS (Host service registry; no asset operations)");
            else output.error("EternalCore ABI selfcheck FAIL");
        });
        auto& hostCommand = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
            "eternal", "永恒模块：运行状态", CommandPermissionLevel::Any);
        hostCommand.overload().text("status").execute([this](CommandOrigin const&, CommandOutput& output) {
            const auto snapshot = host_.snapshot();
            if (!snapshot.enabled) { output.error("EternalHost is not ready"); return; }
            std::string summary = "EternalHost | module ABI 1.0";
            for (const auto& module : snapshot.modules) {
                summary += " | " + module.id + ":" + eternal::host::stateName(module.state);
            }
            output.success(summary);
        });
        self_.getLogger().info("EternalHost enabled | ecore status / ecore selfcheck / eternal status");
        return true;
    } catch (const std::exception& error) {
        if (native_) native_->disable();
        std::string ignored;
        host_.disable(ignored);
        self_.getLogger().error("EternalHost enable failed: {}", error.what());
        return false;
    } catch (...) {
        if (native_) native_->disable();
        std::string ignored;
        host_.disable(ignored);
        return false;
    }
}

bool HostMod::disable() {
    try {
        if (host_.onBoundThread() && host_.isDispatching()) {
            self_.getLogger().warn("EternalHost disable refused while a trusted callback is active");
            return false;
        }
        if (native_) native_->disable();
        if (!host_.onBoundThread() && ll::getGamingStatus() == ll::GamingStatus::Stopping) {
            // LL disables mods before leaveGameSync joins the server thread.
            // Keep the Host image resident and finish only after the OS handle
            // proves that no server-thread module callback can still execute.
            stopDeferred_.store(true);
            self_.getLogger().info("EternalHost shutdown deferred until server thread exit");
            return true;
        }
        std::string error;
        const bool stopped = host_.disable(error);
        if (!stopped) self_.getLogger().error("EternalHost disable failed: {}", error);
        else self_.getLogger().info("EternalHost disabled; module services and subscriptions revoked");
        return stopped;
    } catch (...) { return false; }
}

void HostMod::finishServerStop() noexcept {
    if (!stopDeferred_.exchange(false)) return;
    try {
        std::string error;
        if (!host_.finishStopAfterServerThreadExit(error)) {
            self_.getLogger().error("EternalHost stop cleanup FAIL: {}", error);
            return;
        }
        self_.getLogger().info("EternalHost stop cleanup PASS; internal modules disabled and unloaded");
    } catch (...) {
        self_.getLogger().error("EternalHost stop cleanup FAIL at adapter boundary");
    }
}
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    EternalServerStopHook,
    ll::memory::HookPriority::Normal,
    ServerInstance,
    &ServerInstance::leaveGameSync,
    void
) {
    origin();
    eternal::adapter::HostMod::getInstance().finishServerStop();
}

// LL owns command callbacks. As in Phase 1, live LL hot-unload is deliberately
// unregistered; disable/enable is supported and full replacement uses restart.
LL_REGISTER_MOD(eternal::adapter::HostMod, eternal::adapter::HostMod::getInstance());
