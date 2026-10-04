#include "EternalSDK/abi.h"
#include "ApiService.hpp"

#include <iostream>

int main() {
    auto* api = EternalCore_QueryApi(1, 0);
    if (!api || !eternal::native::apiSelfcheck()) return 1;
    EcVersionInfo small{};
    small.struct_size = sizeof(small) - 1;
    small.struct_version = EC_STRUCT_VERSION;
    EcVersionInfo wrongVersion{};
    wrongVersion.struct_size = sizeof(wrongVersion);
    wrongVersion.struct_version = 2;
    EcVersionInfo oversized{};
    oversized.struct_size = sizeof(oversized) + 8;
    oversized.struct_version = EC_STRUCT_VERSION;
    if (api->get_version(&small) != EC_INVALID_ARGUMENT
        || api->get_version(&wrongVersion) != EC_INVALID_ARGUMENT
        || api->get_version(&oversized) != EC_INVALID_ARGUMENT) return 2;
    for (auto state : {EC_LIFECYCLE_STARTING, EC_LIFECYCLE_READY,
                       EC_LIFECYCLE_DRAINING, EC_LIFECYCLE_STOPPED}) {
        eternal::native::setLifecycle(state);
        if (!eternal::native::apiSelfcheck()) return 3;
    }
    std::cout << "PASS: standalone SDK service version, lifecycle, validation, unsupported capabilities\n";
}
