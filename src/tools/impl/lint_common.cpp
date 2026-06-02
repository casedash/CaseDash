#include "tools/impl/lint_common.h"

#include <stdexcept>

namespace tools::lint {

int FileRecord::LineCount() const {
    return static_cast<int>(lines.size());
}

bool CheckResult::Failed() const {
    return !findings.empty() || !errors.empty();
}

std::vector<std::string> ConfigStrings(const JsonValue& config, std::string_view key) {
    const JsonValue* value = config.Find(key);
    if (value == nullptr || value->IsNull()) {
        return {};
    }
    std::vector<std::string> strings;
    for (const JsonValue& item : value->AsArray()) {
        strings.push_back(item.AsString());
    }
    return strings;
}

std::set<std::string> RequireSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName
) {
    const auto found = suffixGroups.find(std::string(groupName));
    if (found == suffixGroups.end()) {
        throw std::runtime_error(
            std::string(configPath) + " references unknown suffix group " + std::string(groupName)
        );
    }
    return found->second;
}

}  // namespace tools::lint
