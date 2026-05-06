#include "config/config_io.h"

#include <cstdio>

#include "config/config_parser.h"
#include "util/paths.h"

FilePath GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options, const ConfigParseContext& context) {
    AppConfig config = LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig, context);
    ApplyDiagnosticsScaleOverride(config, options);
    return config;
}

bool CanWriteRuntimeConfig(const FilePath& path) {
    if (FileExists(path)) {
        HANDLE file = CreateFileW(path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        CloseHandle(file);
        return true;
    }

    const FilePath parent = path.has_parent_path() ? path.parent_path() : CurrentDirectoryPath();
    wchar_t probeName[80] = {};
    swprintf_s(probeName,
        L".config-write-test-%lu-%llu.tmp",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()));
    const FilePath probePath = parent / probeName;
    HANDLE probe = CreateFileW(probePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(probe);
    return true;
}
