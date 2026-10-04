#pragma once
#include "EternalSDK/Module/module_abi.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eternal::host {
struct ModuleConfig {
    std::string id;
    std::filesystem::path path; // Relative to the Host's configured base directory.
    bool enabled{false};
    bool required{false};
};
struct Candidate {
    std::string id;
    std::filesystem::path path;
    bool enabled{false};
    bool required{false};
};
enum class ModuleState { Disabled, Discovered, Loaded, Enabled, Failed, Quarantined, Skipped, Unloaded };
struct ModuleSnapshot {
    std::string id;
    std::string display_name;
    EmSemVer version{};
    ModuleState state{ModuleState::Discovered};
    std::string diagnostic;
};
struct Snapshot {
    bool loaded{};
    bool enabled{};
    bool quarantined{};
    std::vector<ModuleSnapshot> modules;
    std::size_t service_count{};
    std::size_t subscription_count{};
};
class Library {
public:
    virtual ~Library() = default;
    virtual void* symbol(const char* name) noexcept = 0;
    // Leak/quarantine the image intentionally if safe teardown cannot be proven.
    virtual void quarantine() noexcept = 0;
};
class LibraryLoader {
public:
    virtual ~LibraryLoader() = default;
    virtual std::unique_ptr<Library> open(const std::filesystem::path& path, std::string& error) = 0;
};
using LogSink = std::function<void(uint32_t, std::string_view)>;
class Host final {
public:
    // Trusted native adapter calls retain the same teardown exclusion as events.
    // This C++ guard is deliberately absent from the public Module ABI.
    class TrustedDispatch final {
        friend class Host;
        Host* host_{};
        explicit TrustedDispatch(Host* host) noexcept : host_(host) {}
    public:
        ~TrustedDispatch();
        TrustedDispatch(TrustedDispatch&& other) noexcept;
        TrustedDispatch(const TrustedDispatch&) = delete;
        TrustedDispatch& operator=(const TrustedDispatch&) = delete;
        TrustedDispatch& operator=(TrustedDispatch&&) = delete;
        explicit operator bool() const noexcept { return host_ != nullptr; }
    };
    explicit Host(std::unique_ptr<LibraryLoader> loader = {}, LogSink log = {});
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    static bool discover(const std::filesystem::path& base, const std::vector<ModuleConfig>& config,
                         std::vector<Candidate>& out, std::string& error);
    bool load(const std::vector<Candidate>& candidates, std::string& error);
    // One explicit startup-to-server-thread handoff after successful Load and
    // before any Enable attempt. The adapter must ensure the old thread is idle.
    // Not available to modules; all subsequent calls stay on the bound thread.
    bool bindFirstEnableThread(std::string& error);
    bool onBoundThread() const noexcept;
    [[nodiscard]] TrustedDispatch guardTrustedDispatch() noexcept;
    bool isDispatching() const noexcept;
    // Trusted adapter's terminal stop only: the saved Windows server-thread
    // handle must prove that thread has exited. Stops and unloads modules,
    // permanently rejects Load/Enable, and never makes ordinary Disable migrate.
    bool finishStopAfterServerThreadExit(std::string& error);
    bool enable(std::string& error);
    bool disable(std::string& error);
    bool shutdown(std::string& error);
    Snapshot snapshot() const;
    EmStatus queryService(const EmServiceRequest& request, EmServiceReference& out) noexcept;
    EmStatus publishEvent(const EmEvent& event) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
const char* stateName(ModuleState state) noexcept;
} // namespace eternal::host
