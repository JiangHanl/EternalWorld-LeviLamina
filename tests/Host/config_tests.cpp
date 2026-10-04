#include "Config.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const std::string valid = R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":true,"required":true}]})";
        std::vector<eternal::host::ModuleConfig> config;
        std::string error;
        if (!eternal::host::parseConfig(valid, config, error) || config.size() != 1 || config[0].id != "core")
            throw std::runtime_error("Valid configuration rejected");
        const std::vector<std::string> invalid{
            "", "not json", "[]", "{}", R"({"modules":[]})",
            R"({"modules":[],"unknown":true})",
            R"({"modules":[{"id":"core","id":"core","path":"modules/EternalCore.dll","enabled":true,"required":true}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":false,"required":true}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":true,"required":false}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":"true","required":true}]})",
            R"({"modules":[{"id":"core","path":"../EternalCore.dll","enabled":true,"required":true}]})",
            R"({"modules":[{"id":"core","path":"modules/../EternalCore.dll","enabled":true,"required":true}]})",
            R"({"modules":[{"id":"core","path":"/modules/EternalCore.dll","enabled":true,"required":true}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.exe","enabled":true,"required":true}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":true,"required":true},{"id":"core","path":"modules/Other.dll","enabled":false,"required":false}]})",
            R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":true,"required":true},{"id":"other","path":"modules/EternalCore.dll","enabled":false,"required":false}]})",
        };
        for (const auto& input : invalid) {
            if (eternal::host::parseConfig(input, config, error) || error.empty() || config.size() != 1 || config[0].id != "core")
                throw std::runtime_error("Invalid configuration accepted or previous output mutated");
        }
        if (eternal::host::parseConfig(std::string(256*1024+1, ' '), config, error)) throw std::runtime_error("Configuration size limit ignored");
        const auto nested = R"({"modules":[{"id":"core","path":"modules/EternalCore.dll","enabled":true,"required":true},{"id":"optional","path":"modules/Optional.dll","enabled":false,"required":false}]})";
        if (!eternal::host::parseConfig(nested, config, error) || config.size() != 2) throw std::runtime_error("Separate object keys treated as duplicates");
        std::cout << "PASS configuration types, duplicate keys/IDs/paths, traversal, required Core and atomic parse output\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
