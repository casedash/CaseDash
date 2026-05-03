#include "util/command_line.h"

#include <algorithm>
#include <shellapi.h>

#include "util/utf8.h"

std::wstring TrimWhitespace(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::wstring(first, last);
}

std::wstring StripOuterQuotes(std::wstring value) {
    value = TrimWhitespace(std::move(value));
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::wstring NormalizeWindowsPath(std::wstring value) {
    value = StripOuterQuotes(std::move(value));
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(
        value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::vector<std::wstring> GetCommandLineArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return {};
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return arguments;
}

bool HasSwitch(const std::string& target) {
    const std::wstring wideTarget = WideFromUtf8(target);
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (_wcsicmp(argument.c_str(), wideTarget.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::wstring> GetSwitchValue(const std::wstring& target) {
    const std::vector<std::wstring> arguments = GetCommandLineArguments();
    for (size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (_wcsicmp(arguments[i].c_str(), target.c_str()) == 0) {
            return arguments[i + 1];
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target) {
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (argument.size() > target.size() && _wcsnicmp(argument.c_str(), target.c_str(), target.size()) == 0 &&
            argument[target.size()] == L':') {
            return argument.substr(target.size() + 1);
        }
    }
    return std::nullopt;
}
