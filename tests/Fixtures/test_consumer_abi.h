#pragma once
#include "EternalSDK/Module/module_abi.h"
// Test-only control protocol. Never installed in the SDK or production package.
enum { TEST_CONSUMER_POLL=1, TEST_CONSUMER_OFFLINE=2, TEST_CONSUMER_ONLINE=3,
       TEST_CONSUMER_FAIL_NEXT=4, TEST_CONSUMER_CRASH_AFTER_COMMIT=5,
       TEST_CONSUMER_REVERSE=6, TEST_CONSUMER_STATE=7 };
struct TestConsumerState {
    uint32_t struct_size, enabled;
    uint64_t effects, duplicates, acknowledgements, failures;
    uint64_t first_processed_event, last_processed_event;
    int32_t last_status;
    uint32_t reserved;
};
using TestConsumerControlFn=EmStatus (EM_CALL *)(uint32_t,TestConsumerState*) noexcept;
