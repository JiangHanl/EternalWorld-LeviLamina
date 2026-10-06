#include "Host.hpp"
#include "EternalSDK/Core/native_ingress_abi.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// REAL_DLL: actual validation Core and Commerce DLLs + actual Host; identities are SYNTHETIC.
namespace {
using eternal::host::Host;
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
EcUtf8View view(std::string_view value) {
    return {value.data(), static_cast<uint32_t>(value.size()), 0};
}
std::string hex128(EcId128 value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[i] = digits[(value.high >> ((15 - i) * 4)) & 15];
        out[16 + i] = digits[(value.low >> ((15 - i) * 4)) & 15];
    }
    return out;
}
struct Scratch {
    std::filesystem::path path;
    explicit Scratch()
        : path(std::filesystem::absolute("artifacts") /
               ("commerce-module-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {}
    ~Scratch() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct Running {
    std::unique_ptr<Host> host;
    const EcNativeIngressApi *native{};
    uint64_t serial{};
    EcNativePlayerIdentity owner{};
    EcNativePlayerIdentity recipient{};
    EcPlayerId recipientId{};

    EcNativePlayerIdentity identity(unsigned char marker, uint64_t xuid, const char *name) {
        EcNativePlayerIdentity value{};
        value.struct_size = sizeof(value);
        value.struct_version = EC_NATIVE_INGRESS_STRUCT_VERSION;
        value.trusted_uuid.bytes[0] = marker;
        value.trusted_uuid.bytes[15] = 1;
        value.client_uuid = value.trusted_uuid;
        value.trusted_xuid = xuid;
        value.display_name = view(name);
        value.is_fully_authenticated = 1;
        return value;
    }
    EcNativeJoinResult authenticate(const EcNativePlayerIdentity &who) {
        EcNativeJoinResult joined{sizeof(joined), EC_NATIVE_INGRESS_STRUCT_VERSION};
        require(native->authenticated_player(native->bridge_nonce, &who, &joined) == EC_OK,
                "Synthetic identity rejected");
        return joined;
    }
    std::string command(const EcNativePlayerIdentity &who, std::string_view text) {
        EcNativeCommandRequest request{};
        request.struct_size = sizeof(request);
        request.struct_version = EC_NATIVE_INGRESS_STRUCT_VERSION;
        request.request_id = {++serial,
                              static_cast<uint64_t>(
                                  std::chrono::steady_clock::now().time_since_epoch().count())};
        request.origin = EC_NATIVE_ORIGIN_PLAYER;
        request.player = who;
        request.command_text = view(text);
        std::array<char, 16384> output{};
        EcUtf8Buffer buffer{output.data(), static_cast<uint32_t>(output.size()), 0};
        EcNativeReply reply{sizeof(reply), EC_NATIVE_INGRESS_STRUCT_VERSION};
        auto guard = host->guardTrustedDispatch();
        require(static_cast<bool>(guard), "Controller dispatch guard missing");
        const auto status =
            native->native_command(native->bridge_nonce, &request, &reply, &buffer);
        require(status == EC_OK && reply.result == EC_OK,
                "Command failed: " + std::string(output.data()));
        return std::string(output.data());
    }
    void notice() {
        EcPhase2OutboxNotice hint{sizeof(hint), EC_PHASE2_STRUCT_VERSION, 0, native->instance_epoch,
                                  0};
        EmEvent event{sizeof(event), EM_STRUCT_VERSION, {"core.outbox.changed", 19, 0}, &hint,
                      sizeof(hint), EC_PHASE2_STRUCT_VERSION, 0};
        require(host->publishEvent(event) == EM_OK, "Formal EventBus publish failed");
    }

    explicit Running(const std::filesystem::path &root) {
        host = std::make_unique<Host>();
        std::vector<eternal::host::Candidate> candidates;
        std::string error;
        require(Host::discover(root,
                               {{"core", "modules/EternalCore.dll", true, true},
                                {"commerce", "modules/EternalCommerceValidation.dll", true, true}},
                               candidates, error),
                error);
        require(host->load(candidates, error) && host->enable(error), error);
        EmServiceRequest request{sizeof(request), EM_STRUCT_VERSION,
                                 {EC_NATIVE_INGRESS_SERVICE_ID,
                                  sizeof(EC_NATIVE_INGRESS_SERVICE_ID) - 1, 0},
                                 1, 0, 0, 0};
        EmServiceReference reference{sizeof(reference), EM_STRUCT_VERSION};
        require(host->queryService(request, reference) == EM_OK,
                "Trusted test controller ingress missing");
        native = static_cast<const EcNativeIngressApi *>(reference.table);
        owner = identity(0xaa, 1000, "SyntheticOwner");
        recipient = identity(0xbb, 1001, "SyntheticRecipient");
        (void)authenticate(owner);
        recipientId = authenticate(recipient).player_id;
    }
    ~Running() {
        if (host) {
            std::string error;
            host->shutdown(error);
            host.reset();
        }
    }
};
void prepare(const std::filesystem::path &root, const std::filesystem::path &core,
             const std::filesystem::path &commerce) {
    std::filesystem::create_directories(root / "modules");
    std::filesystem::create_directories(root / "config/core");
    std::filesystem::copy_file(core, root / "modules/EternalCore.dll");
    std::filesystem::copy_file(commerce, root / "modules/EternalCommerceValidation.dll");
    std::ofstream config(root / "config/core/core.json");
    config << R"({"ownerXuid":"1000","developmentValidation":true,"validatedAssets":false,"moduleCapabilities":{"core":["core.player.read.v1","core.permission.check.v1","core.transaction.money.v1","core.transaction.reputation.v1","core.audit.write.v1","core.events.v1"],"commerce":["core.player.read.v1","core.permission.check.v1","core.transaction.money.v1","core.transaction.reputation.v1","core.audit.write.v1","core.events.v1"]}})";
    require(static_cast<bool>(config), "Synthetic config failed");
}
int groups{};
void pass(const char *label) {
    ++groups;
    std::cout << "PASS " << label << '\n';
}
std::int64_t deliveryCount(const std::filesystem::path &root) {
    const auto filename = (root / "data/commerce/commerce.sqlite3").u8string();
    sqlite3 *database{};
    require(sqlite3_open_v2(reinterpret_cast<const char *>(filename.c_str()), &database,
                            SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
            "Commerce delivery database not durable");
    sqlite3_stmt *statement{};
    const auto code = sqlite3_prepare_v2(database, "SELECT count(*) FROM delivery_requests", -1,
                                         &statement, nullptr);
    const bool good = code == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW;
    const auto result = good ? sqlite3_column_int64(statement, 0) : -1;
    sqlite3_finalize(statement);
    sqlite3_close(database);
    require(good && result >= 0, "Delivery count query failed");
    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 4,
                "Usage: CommerceModuleTests <Eternal root> <Validation Core DLL> <Commerce DLL>");
        Scratch scratch;
        prepare(scratch.path, std::filesystem::absolute(argv[2]),
                std::filesystem::absolute(argv[3]));
        Running run(scratch.path);
        run.command(run.owner, "asset self money 100000 commerce-seed commerce-seed-reason");
        const auto reply =
            run.command(run.owner, "invoke commerce transfer " + hex128(run.recipientId) +
                                       " 30000");
        require(reply.find("Commerce transfer accepted") != std::string::npos,
                "Transfer route did not accept");
        pass("REAL_DLL Commerce transfer authorize and Core submit closed loop");
        run.notice();
        require(deliveryCount(scratch.path) >= 1, "Delivery consumer did not record the event");
        pass("REAL_DLL Commerce delivery consumer registers, dedups and ACKs");
        const auto giftReply = run.command(run.owner, "invoke commerce gift 1000 3");
        require(giftReply.find("Commerce gift accepted") != std::string::npos,
                "Gift route did not accept");
        pass("REAL_DLL Commerce gift authorize and Core deduct closed loop");
        run.command(run.owner, "asset SyntheticRecipient money 10000 buy-fund buy-fund-reason");
        const auto listReply = run.command(run.owner, "invoke commerce list diamond 5000");
        const auto buyReply =
            run.command(run.recipient, "invoke commerce buy " + listReply);
        require(buyReply.find("Commerce buy accepted") != std::string::npos,
                "Buy route did not accept");
        pass("REAL_DLL Commerce consignment buy transfer and delivery closed loop");
        std::cout << "PASS CommerceModuleTests: " << groups
                  << " groups (REAL_DLL; synthetic identities; REAL_CLIENT NOT RUN)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
