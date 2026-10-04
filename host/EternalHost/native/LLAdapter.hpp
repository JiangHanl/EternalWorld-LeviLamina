#pragma once
#include "runtime/Host.hpp"
#include <memory>

namespace eternal::adapter {
// The engine-facing boundary only copies authenticated engine identity and
// renders Core-owned diagnostics/forms. All authorization stays inside Core.
class LLAdapter final {
public:
    LLAdapter(eternal::host::Host&, eternal::host::LogSink);
    ~LLAdapter();
    LLAdapter(const LLAdapter&) = delete;
    LLAdapter& operator=(const LLAdapter&) = delete;
    bool enable();
    void disable() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
