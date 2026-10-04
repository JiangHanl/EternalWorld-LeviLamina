#include "ApiService.hpp"
#include "EternalSDK/abi.h"

#include <atomic>
#include <chrono>

namespace {
std::atomic<std::uint32_t> state{EC_LIFECYCLE_STARTING};
const std::uint64_t epoch = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

template <class T> bool validOutput(const T* out) noexcept {
    return out && out->struct_size == sizeof(T) && out->struct_version == EC_STRUCT_VERSION;
}

EcStatus EC_CALL version(EcVersionInfo* out) noexcept {
    if (!validOutput(out)) return EC_INVALID_ARGUMENT;
    *out = {sizeof(EcVersionInfo), EC_STRUCT_VERSION, EC_API_MAJOR, EC_API_MINOR,
            0, 1, 0, state.load(), epoch, 0};
    return EC_OK;
}

EcStatus EC_CALL features(EcFeatureInfo* out) noexcept {
    if (!validOutput(out)) return EC_INVALID_ARGUMENT;
    *out = {sizeof(EcFeatureInfo), EC_STRUCT_VERSION, EC_PHASE1_FEATURES,
            EC_THREAD_DOMAIN_GAME_THREAD, 0, 0};
    return EC_OK;
}

EcStatus EC_CALL identity(const EcIdentityRequest*, EcIdentitySnapshot*, EcUtf8Buffer*) noexcept {
    return EC_UNSUPPORTED;
}
EcStatus EC_CALL coin(const EcCoinRequest*, EcCoinSnapshot*) noexcept { return EC_UNSUPPORTED; }
EcStatus EC_CALL transfer(const EcTransferRequest*, EcTransferSubmission*) noexcept { return EC_UNSUPPORTED; }
EcStatus EC_CALL receipt(const EcReceiptRequest*, EcTransferReceipt*) noexcept { return EC_UNSUPPORTED; }

const EternalCoreApi api = {
    sizeof(EternalCoreApi), EC_API_MAJOR, EC_API_MINOR, 0,
    version, features, identity, coin, transfer, receipt, {0, 0, 0, 0}
};
}

extern "C" EC_EXPORT const EternalCoreApi* EC_CALL EternalCore_QueryApi(
    std::uint32_t major, std::uint32_t minMinor) noexcept {
    return major == EC_API_MAJOR && minMinor <= EC_API_MINOR ? &api : nullptr;
}

namespace eternal::native {
void setLifecycle(std::uint32_t value) noexcept { state.store(value); }
std::uint32_t lifecycle() noexcept { return state.load(); }

bool apiSelfcheck() noexcept {
    auto* table = EternalCore_QueryApi(EC_API_MAJOR, EC_API_MINOR);
    EcVersionInfo info{};
    info.struct_size = sizeof(info);
    info.struct_version = EC_STRUCT_VERSION;
    EcFeatureInfo caps{};
    caps.struct_size = sizeof(caps);
    caps.struct_version = EC_STRUCT_VERSION;
    return table && table->struct_size == sizeof(EternalCoreApi)
        && !EternalCore_QueryApi(EC_API_MAJOR + 1, 0)
        && !EternalCore_QueryApi(EC_API_MAJOR, EC_API_MINOR + 1)
        && table->get_version(&info) == EC_OK && info.lifecycle == lifecycle()
        && table->get_version(nullptr) == EC_INVALID_ARGUMENT
        && table->get_features(&caps) == EC_OK && caps.enabled == 0
        && table->read_identity(nullptr, nullptr, nullptr) == EC_UNSUPPORTED
        && table->read_coin(nullptr, nullptr) == EC_UNSUPPORTED
        && table->submit_transfer(nullptr, nullptr) == EC_UNSUPPORTED
        && table->poll_receipt(nullptr, nullptr) == EC_UNSUPPORTED;
}
}
