#pragma once
#include "ll/api/mod/NativeMod.h"
#include "runtime/Host.hpp"

namespace eternal::adapter {
class HostMod final {
public:
    static HostMod& getInstance();
    HostMod();
    bool load();
    bool enable();
    bool disable();
private:
    ll::mod::NativeMod& self_;
    eternal::host::Host host_;
};
}
