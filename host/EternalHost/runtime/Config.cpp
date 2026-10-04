#include "Config.hpp"
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>

namespace eternal::host {
bool parseConfig(std::string_view text, std::vector<ModuleConfig>& out, std::string& error) {
    try {
        if (text.empty() || text.size() > 256 * 1024) { error = "modules configuration is empty or exceeds 256 KiB"; return false; }
        using Json = nlohmann::json;
        std::vector<std::set<std::string>> objectKeys;
        bool duplicateKey = false;
        const auto parsed = Json::parse(text, [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
            else if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
            else if (event == Json::parse_event_t::key && !objectKeys.back().insert(value.get<std::string>()).second) duplicateKey = true;
            return true;
        });
        if (duplicateKey) { error = "duplicate JSON object key in modules configuration"; return false; }
        if (!parsed.is_object() || parsed.size() != 1 || !parsed.contains("modules") || !parsed["modules"].is_array()) {
            error = "configuration must contain only a modules array"; return false;
        }
        if (parsed["modules"].empty() || parsed["modules"].size() > 64) { error = "modules array must contain 1..64 entries"; return false; }
        std::vector<ModuleConfig> result;
        std::set<std::string> ids;
        std::set<std::filesystem::path> paths;
        bool core = false;
        for (const auto& entry : parsed["modules"]) {
            if (!entry.is_object() || entry.size() != 4 || !entry.contains("id") || !entry["id"].is_string()
                || !entry.contains("path") || !entry["path"].is_string() || !entry.contains("enabled")
                || !entry["enabled"].is_boolean() || !entry.contains("required") || !entry["required"].is_boolean()) {
                error = "each module requires id, path, enabled and required with exact types"; return false;
            }
            const auto id = entry["id"].get<std::string>();
            const auto pathText = entry["path"].get<std::string>();
            bool validId = !id.empty() && id.size() <= 64 && id[0] >= 'a' && id[0] <= 'z';
            for (const auto c : id) validId = validId && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
            if (!validId || !ids.insert(id).second) { error = "invalid or duplicate module id"; return false; }
            if (pathText.empty() || pathText.size() > 512 || pathText.find('\0') != std::string::npos
                || pathText.find(':') != std::string::npos || pathText.find('\\') != std::string::npos) {
                error = "module path must be a relative modules/*.dll path using forward slashes"; return false;
            }
            const auto path = std::filesystem::path(std::u8string(pathText.begin(), pathText.end()));
            if (path.is_absolute() || path.has_root_path() || path.extension() != ".dll" || path.begin()->string() != "modules") {
                error = "module DLL must be inside modules/"; return false;
            }
            for (const auto& part : path) {
                if (part == ".." || part == "." || part.empty()) { error = "module path traversal is forbidden"; return false; }
            }
            if (!paths.insert(path.lexically_normal()).second) { error = "duplicate module DLL path"; return false; }
            ModuleConfig config{id, path, entry["enabled"].get<bool>(), entry["required"].get<bool>()};
            if (config.required && !config.enabled) { error = "a required module cannot be disabled"; return false; }
            if (id == EM_CORE_MODULE_ID) {
                core = config.enabled && config.required;
                if (!core) { error = "Core must be enabled and required"; return false; }
            }
            result.push_back(std::move(config));
        }
        if (!core) { error = "required Core module missing from configuration"; return false; }
        out = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = "invalid modules configuration: " + std::string(exception.what());
        return false;
    }
}

bool loadConfig(const std::filesystem::path& file, std::vector<ModuleConfig>& out, std::string& error) {
    try {
        if (std::filesystem::file_size(file) > 256 * 1024) { error = "modules configuration exceeds 256 KiB"; return false; }
        std::ifstream input(file, std::ios::binary);
        if (!input) { error = "cannot open modules configuration"; return false; }
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        return parseConfig(text, out, error);
    } catch (const std::exception& exception) {
        error = "cannot load modules configuration: " + std::string(exception.what());
        return false;
    }
}
}
