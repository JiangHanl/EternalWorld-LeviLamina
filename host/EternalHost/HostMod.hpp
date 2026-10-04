#pragma once
#include "ll/api/mod/NativeMod.h"
#include "runtime/Host.hpp"
#include <atomic>

namespace eternal::adapter {
class HostMod final {
public:
    static HostMod& getInstance();
    HostMod();
    bool load();
    bool enable();
    bool disable();
    void finishServerStop() noexcept;
private:
    ll::mod::NativeMod& self_;
    eternal::host::Host host_;
    bool firstEnableThreadBound_ = false;
    std::atomic_bool stopDeferred_ = false;
};
}
