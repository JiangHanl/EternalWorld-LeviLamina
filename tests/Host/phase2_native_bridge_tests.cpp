#include "../../host/EternalHost/runtime/Host.hpp"
#include "EternalSDK/Core/native_ingress_abi.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

using namespace eternal::host;
namespace {
struct Fixture {
    std::string id;
    EmModuleDescriptor descriptor{};
    EmDependency dependency{};
    EmHostContext context{};
};
struct Binding {
    EternalCorePhase2Api api{};
    std::string module;
    uint64_t generation{}, caps{};
    bool active{true};
};
std::array<Fixture, 3> modules;
std::unordered_map<uint64_t, Binding> bindings;
std::vector<EcNativeModuleBindingRequest> requests;
std::vector<std::string> boundNames, trace;
EcNativeIngressApi native{};
EternalCorePhase2Api discovery{};
std::filesystem::path folder;
bool publishIngress{}, malformedBinding{}, denyBinding{};
uint64_t epoch{}, serial{};
int groupCount{}, bindCalls{}, revokeCalls{};
const uint32_t statusTable = 9;
Host *probe{};
bool revokeBeforeServiceDelete{};
EmUtf8View emView(std::string_view s) { return {s.data(), static_cast<uint32_t>(s.size()), 0}; }
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
bool equal(EcId128 a, EcId128 b) { return a.low == b.low && a.high == b.high; }
EcStatus EC_CALL readAsset(const EcPhase2AssetRequest *request, EcPhase2AssetSnapshot *) noexcept {
    if (!request)
        return EC_INVALID_ARGUMENT;
    const auto found = bindings.find(request->meta.caller_context.low);
    return found != bindings.end() && found->second.active ? EC_OK : EC_REVOKED;
}
EcStatus EC_CALL bindModule(EcNativeBridgeToken token, const EcNativeModuleBindingRequest *request,
                            EcNativeModuleBindingResult *out) noexcept {
    if (!equal(token, native.bridge_nonce) || !request || !out)
        return EC_DENIED;
    ++bindCalls;
    if (denyBinding)
        return EC_DENIED;
    const auto name = std::string(request->module_id.data, request->module_id.length);
    boundNames.push_back(name);
    requests.push_back(*request);
    requests.back().module_id = {};
    trace.push_back("bind:" + name);
    auto [it, inserted] = bindings.emplace(++serial, Binding{});
    if (!inserted)
        return EC_CONFLICT;
    auto &b = it->second;
    b.module = name;
    b.generation = request->module_generation;
    b.caps = request->approved_module_capabilities;
    b.api = discovery;
    b.api.caller_context = {serial, epoch};
    b.api.caller_generation = b.generation;
    b.api.instance_epoch = epoch;
    b.api.read_asset = readAsset;
    *out = {sizeof(*out),
            EC_NATIVE_INGRESS_STRUCT_VERSION,
            &b.api,
            sizeof(b.api),
            0,
            b.api.caller_context,
            b.generation,
            epoch,
            0};
    if (malformedBinding)
        b.api.caller_context = {};
    return EC_OK;
}
EcStatus EC_CALL revokeModule(EcNativeBridgeToken token,
                              const EcNativeModuleRevokeRequest *request) noexcept {
    if (!equal(token, native.bridge_nonce) || !request)
        return EC_DENIED;
    const auto found = bindings.find(request->caller_context.low);
    if (found == bindings.end())
        return EC_NOT_FOUND;
    ++revokeCalls;
    auto &b = found->second;
    trace.push_back("revoke:" + b.module);
    if (probe) {
        const auto serviceId = b.module + ".status";
        EmServiceRequest q{sizeof(q), EM_STRUCT_VERSION, emView(serviceId), 1, 0, 0, 0};
        EmServiceReference r{};
        r.struct_size = sizeof(r);
        r.struct_version = EM_STRUCT_VERSION;
        if (probe->queryService(q, r) == EM_OK)
            revokeBeforeServiceDelete = true;
    }
    b.active = false;
    return EC_OK;
}
void reset() {
    probe = nullptr;
    revokeBeforeServiceDelete = false;
    bindings.clear();
    requests.clear();
    boundNames.clear();
    trace.clear();
    bindCalls = revokeCalls = 0;
    serial = 0;
    epoch = 0;
    publishIngress = true;
    malformedBinding = denyBinding = false;
    discovery = {};
    discovery.v1_0.struct_size = sizeof(EternalCoreApi);
    discovery.v1_0.api_major = EC_API_MAJOR;
    discovery.struct_size = sizeof(discovery);
    discovery.struct_version = EC_PHASE2_STRUCT_VERSION;
    discovery.api_major = EC_PHASE2_API_MAJOR;
    discovery.api_minor = EC_PHASE2_API_MINOR;
    native = {};
    native.struct_size = sizeof(native);
    native.struct_version = EC_NATIVE_INGRESS_STRUCT_VERSION;
    native.api_major = EC_NATIVE_INGRESS_MAJOR;
    native.api_minor = EC_NATIVE_INGRESS_MINOR;
    native.bind_module = bindModule;
    native.revoke_module = revokeModule;
    for (size_t i = 0; i < modules.size(); ++i) {
        modules[i] = {};
        auto &m = modules[i];
        m.id = i == 0 ? "core" : i == 1 ? "market" : "quests";
        m.descriptor = {sizeof(m.descriptor),
                        EM_STRUCT_VERSION,
                        emView(m.id),
                        emView(m.id),
                        {0, 2, 0, 0},
                        EM_ABI_MAJOR,
                        EM_ABI_MINOR,
                        EM_HOST_CAPABILITIES,
                        0,
                        i == 0 ? EC_MODULE_CAP_ALL : 0,
                        nullptr,
                        0,
                        0,
                        {0, 0}};
        if (i != 0) {
            m.dependency = {sizeof(m.dependency),
                            EM_STRUCT_VERSION,
                            emView("core"),
                            {0, 2, 0, 0},
                            EC_MODULE_CAP_PLAYER_READ | EC_MODULE_CAP_MONEY,
                            0,
                            0};
            m.descriptor.dependencies = &m.dependency;
            m.descriptor.dependency_count = 1;
        }
    }
}
template <size_t I> const EmModuleDescriptor *EM_CALL descriptor() noexcept {
    return &modules[I].descriptor;
}
template <size_t I> EmStatus EM_CALL load(const EmHostContext *context) noexcept {
    modules[I].context = *context;
    return EM_OK;
}
template <size_t I> EmStatus EM_CALL enable() noexcept {
    auto &m = modules[I];
    trace.push_back("enable:" + m.id);
    const auto serviceId = m.id + ".status";
    EmServiceOffer status{sizeof(status),
                          EM_STRUCT_VERSION,
                          emView(serviceId),
                          1,
                          0,
                          0,
                          &statusTable,
                          sizeof(statusTable),
                          0};
    auto result = m.context.publish_service(m.context.instance, &status);
    if (result != EM_OK)
        return result;
    if constexpr (I == 0) {
        ++epoch;
        native.instance_epoch = epoch;
        native.bridge_nonce = {1234, epoch};
        discovery.instance_epoch = epoch;
        EmServiceOffer p2{sizeof(p2),          EM_STRUCT_VERSION,   emView(EC_PHASE2_SERVICE_ID),
                          EC_PHASE2_API_MAJOR, EC_PHASE2_API_MINOR, EC_MODULE_CAP_ALL,
                          &discovery,          sizeof(discovery),   0};
        result = m.context.publish_service(m.context.instance, &p2);
        if (result != EM_OK)
            return result;
        if (publishIngress) {
            EmServiceOffer ingress{sizeof(ingress),
                                   EM_STRUCT_VERSION,
                                   emView(EC_NATIVE_INGRESS_SERVICE_ID),
                                   EC_NATIVE_INGRESS_MAJOR,
                                   EC_NATIVE_INGRESS_MINOR,
                                   0,
                                   &native,
                                   sizeof(native),
                                   0};
            result = m.context.publish_service(m.context.instance, &ingress);
        }
    }
    return result;
}
template <size_t I> EmStatus EM_CALL disable() noexcept {
    trace.push_back("disable:" + modules[I].id);
    if constexpr (I == 0)
        for (auto &[id, b] : bindings) {
            (void)id;
            b.active = false;
        }
    return EM_OK;
}
template <size_t I> EmStatus EM_CALL unload() noexcept {
    trace.push_back("unload:" + modules[I].id);
    return EM_OK;
}
const std::array<EmGetDescriptorFn, 3> descriptors{descriptor<0>, descriptor<1>, descriptor<2>};
const std::array<EmLoadFn, 3> loaders{load<0>, load<1>, load<2>};
const std::array<EmLifecycleFn, 3> enablers{enable<0>, enable<1>, enable<2>},
    disablers{disable<0>, disable<1>, disable<2>}, unloaders{unload<0>, unload<1>, unload<2>};
class FakeLibrary final : public Library {
    size_t index_;

  public:
    explicit FakeLibrary(size_t i) : index_(i) {}
    void *symbol(const char *name) noexcept override {
        const std::string_view n(name);
        if (n == EM_GET_DESCRIPTOR_EXPORT)
            return reinterpret_cast<void *>(descriptors[index_]);
        if (n == EM_LOAD_EXPORT)
            return reinterpret_cast<void *>(loaders[index_]);
        if (n == EM_ENABLE_EXPORT)
            return reinterpret_cast<void *>(enablers[index_]);
        if (n == EM_DISABLE_EXPORT)
            return reinterpret_cast<void *>(disablers[index_]);
        if (n == EM_UNLOAD_EXPORT)
            return reinterpret_cast<void *>(unloaders[index_]);
        return nullptr;
    }
    void quarantine() noexcept override {}
};
class FakeLoader final : public LibraryLoader {
  public:
    std::unique_ptr<Library> open(const std::filesystem::path &path, std::string &) override {
        for (size_t i = 0; i < modules.size(); ++i)
            if (path.stem() == modules[i].id)
                return std::make_unique<FakeLibrary>(i);
        return {};
    }
};
std::vector<Candidate> candidates() {
    return {{"market", folder / "market.dll", true, true},
            {"core", folder / "core.dll", true, true},
            {"quests", folder / "quests.dll", true, true}};
}
EmServiceRequest request(std::string_view id, uint64_t caps = 0) {
    return {sizeof(EmServiceRequest),
            EM_STRUCT_VERSION,
            emView(id),
            1,
            id == EC_PHASE2_SERVICE_ID ? EC_PHASE2_API_MINOR : 0,
            caps,
            0};
}
EmServiceReference reference() {
    EmServiceReference r{};
    r.struct_size = sizeof(r);
    r.struct_version = EM_STRUCT_VERSION;
    return r;
}
EmStatus query(size_t who, const EmServiceRequest &q, EmServiceReference &r) {
    return modules[who].context.query_service(modules[who].context.instance, &q, &r);
}
std::unique_ptr<Host> started() {
    auto h = std::make_unique<Host>(std::make_unique<FakeLoader>());
    std::string error;
    require(h->load(candidates(), error) && h->enable(error), error);
    return h;
}
template <class F> void test(const std::string &label, F fn) {
    reset();
    fn();
    ++groupCount;
    std::cout << "PASS " << label << '\n';
}
} // namespace
int main() {
    try {
        folder = std::filesystem::temp_directory_path() / "EternalHostPhase2NativeBridgeFixtures";
        std::filesystem::create_directories(folder);
        for (auto id : {"core", "market", "quests"})
            std::ofstream(folder / (std::string(id) + ".dll")) << "mock";
        test("private ingress is denied to every module including Core, trusted Host may query",
             [] {
                 auto host = started();
                 for (size_t i = 0; i < 3; ++i) {
                     auto r = reference();
                     require(query(i, request(EC_NATIVE_INGRESS_SERVICE_ID), r) == EM_CONFLICT,
                             "Module obtained private ingress");
                     require(query(i, request("core.native.missing"), r) == EM_CONFLICT,
                             "Private policy depends on service existence");
                 }
                 auto r = reference();
                 require(host->queryService(request(EC_NATIVE_INGRESS_SERVICE_ID), r) == EM_OK &&
                             r.table == &native,
                         "Trusted Host ingress rejected");
             });
        test("Phase2 discovery substitutes real Node ID and declared-request capability "
             "intersection",
             [] {
                 auto host = started();
                 auto r = reference();
                 require(query(1, request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_PLAYER_READ), r) ==
                             EM_OK,
                         "Scoped query failed");
                 const auto *api = static_cast<const EternalCorePhase2Api *>(r.table);
                 require(api != &discovery && api->caller_context.low != 0 &&
                             boundNames == std::vector<std::string>{"market"},
                         "Unbound API or caller spoof");
                 require(requests.size() == 1 && requests[0].module_generation > 0 &&
                             requests[0].approved_module_capabilities == EC_MODULE_CAP_PLAYER_READ,
                         "Module generation/cap substitution wrong");
                 require(requests[0].approved_permissions == EC_P2_PERMISSION_ALL &&
                             requests[0].money_limit_per_request == INT64_MAX &&
                             requests[0].budget_period_ms == 60000,
                         "Host protocol upper bounds wrong");
             });
        test("undeclared Core dependency and over-declared capability requests are refused", [] {
            modules[1].descriptor.dependency_count = 0;
            auto host = started();
            auto r = reference();
            require(query(1, request(EC_PHASE2_SERVICE_ID), r) == EM_CONFLICT,
                    "Undeclared Core allowed");
            require(query(2, request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_REPUTATION), r) ==
                        EM_UNSUPPORTED,
                    "Undeclared capabilities allowed");
            require(query(2, request(EC_PHASE2_SERVICE_ID, UINT64_C(1) << 63), r) == EM_UNSUPPORTED,
                    "Unknown capabilities allowed");
            require(bindCalls == 0, "Rejected query minted binding");
        });
        test("scoped binding cache is shared for same-provider subset queries only", [] {
            auto host = started();
            auto first = reference(), second = reference();
            const auto caps = EC_MODULE_CAP_PLAYER_READ | EC_MODULE_CAP_MONEY;
            require(query(1, request(EC_PHASE2_SERVICE_ID, caps), first) == EM_OK &&
                        query(1, request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_PLAYER_READ),
                              second) == EM_OK,
                    "Cache query failed");
            require(bindCalls == 1 && first.table == second.table &&
                        first.generation == second.generation,
                    "Binding not cached");
            auto other = reference();
            require(query(2, request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_PLAYER_READ), other) ==
                            EM_OK &&
                        other.table != first.table,
                    "Two module scopes shared API");
            require(query(2, request(EC_PHASE2_SERVICE_ID, caps), other) == EM_UNSUPPORTED &&
                        bindCalls == 2,
                    "Immutable binding expanded capabilities");
        });
        test("Disable revokes before deleting registrations and re-enable gets fresh generations",
             [] {
                 auto host = started();
                 auto first = reference();
                 const auto req = request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_PLAYER_READ);
                 require(query(1, req, first) == EM_OK, "Initial scoped query failed");
                 const auto *old = static_cast<const EternalCorePhase2Api *>(first.table);
                 const auto generation = old->caller_generation;
                 probe = host.get();
                 std::string error;
                 require(host->disable(error), error);
                 probe = nullptr;
                 require(revokeCalls == 1 && revokeBeforeServiceDelete,
                         "Revoke happened after service deletion");
                 const auto revoke = std::find(trace.begin(), trace.end(), "revoke:market"),
                            disabled = std::find(trace.begin(), trace.end(), "disable:market");
                 require(revoke < disabled && host->snapshot().service_count == 0,
                         "Disable did not revoke before callback");
                 EcPhase2AssetRequest read{};
                 read.meta.caller_context = old->caller_context;
                 EcPhase2AssetSnapshot out{};
                 require(old->read_asset(&read, &out) == EC_REVOKED, "Old context remains usable");
                 require(host->enable(error), error);
                 auto renewed = reference();
                 require(query(1, req, renewed) == EM_OK, "Renewed query failed");
                 const auto *current = static_cast<const EternalCorePhase2Api *>(renewed.table);
                 require(current->caller_generation > generation &&
                             renewed.generation > first.generation &&
                             current->instance_epoch > old->instance_epoch,
                         "Re-enable reused generation/epoch");
             });
        test("missing private ingress never returns shared unbound Phase2 table", [] {
            publishIngress = false;
            auto host = started();
            auto r = reference();
            r.table = &discovery;
            require(query(1, request(EC_PHASE2_SERVICE_ID), r) == EM_NOT_FOUND &&
                        r.table == nullptr && bindCalls == 0,
                    "Missing ingress fell back to unbound table");
        });
        test("Core independent default denial is returned without a scoped binding", [] {
            denyBinding = true;
            auto host = started();
            auto r = reference();
            require(query(1, request(EC_PHASE2_SERVICE_ID, EC_MODULE_CAP_MONEY), r) ==
                            EM_CONFLICT &&
                        !r.table,
                    "Core policy denial was bypassed");
        });
        test("malformed scoped result is rejected and its issued context is revoked", [] {
            malformedBinding = true;
            auto host = started();
            auto r = reference();
            require(query(1, request(EC_PHASE2_SERVICE_ID), r) == EM_ABI_MISMATCH && !r.table &&
                        revokeCalls == 1,
                    "Malformed scoped table leaked");
        });
        test("Core disable invalidates all module scoped tables", [] {
            auto host = started();
            auto a = reference(), b = reference();
            const auto req = request(EC_PHASE2_SERVICE_ID);
            require(query(1, req, a) == EM_OK && query(2, req, b) == EM_OK,
                    "Initial bindings failed");
            std::string error;
            require(host->disable(error), error);
            require(revokeCalls == 2, "Not all bindings revoked");
            for (const auto &[id, binding] : bindings) {
                (void)id;
                require(!binding.active, "Binding active after Core stop");
            }
            auto r = reference();
            require(host->queryService(request(EC_PHASE2_SERVICE_ID), r) == EM_NOT_FOUND,
                    "Core service survived Disable");
        });
        for (auto id : {"core", "market", "quests"})
            std::filesystem::remove(folder / (std::string(id) + ".dll"));
        std::filesystem::remove(folder);
        std::cout << "PASS " << groupCount
                  << " Phase 2 Host native bridge groups (mock only; BDS NOT RUN)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
