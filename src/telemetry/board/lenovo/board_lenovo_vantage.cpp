#include "telemetry/board/lenovo/board_lenovo_vantage.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/board/lenovo/board_lenovo_vantage_bridge.h"
#include "telemetry/fps_service_protocol.h"
#include "telemetry/impl/system_info_support.h"
#include "util/file_path.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"
#include "util/win32_format.h"

namespace {

constexpr char kLenovoProviderName[] = "Lenovo";
constexpr char kLenovoDriverLibrary[] = "Lenovo Hardware Scan LdeApi";
constexpr DWORD kPipeConnectTimeoutMs = 100;
constexpr DWORD kPipeReadChunkBytes = 4096;
constexpr DWORD kMaximumPipeResponseBytes = 16 * 1024;
constexpr int kSensorRetrySampleInterval = 10;
constexpr std::chrono::seconds kDirectSnapshotRefreshInterval{5};

struct LenovoHardwareScanSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

class Handle {
public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}

    ~Handle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HANDLE Get() const {
        return handle_;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

std::string Utf8FromNullableWide(const wchar_t* text) {
    return text != nullptr ? Utf8FromWide(text) : std::string();
}

bool DirectoryExists(const FilePath& path) {
    const std::wstring wide = path.Wide();
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FilePath> ProgramDataDirectory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return FilePath(std::wstring(buffer.data(), length));
}

std::vector<int> ParseVersionParts(const std::wstring& text) {
    std::vector<int> parts;
    int value = 0;
    bool hasDigits = false;
    for (const wchar_t ch : text) {
        if (ch >= L'0' && ch <= L'9') {
            value = value * 10 + static_cast<int>(ch - L'0');
            hasDigits = true;
            continue;
        }
        if (ch == L'.' && hasDigits) {
            parts.push_back(value);
            value = 0;
            hasDigits = false;
            continue;
        }
        return {};
    }
    if (hasDigits) {
        parts.push_back(value);
    }
    return parts;
}

bool VersionGreater(const std::wstring& left, const std::wstring& right) {
    const std::vector<int> leftParts = ParseVersionParts(left);
    const std::vector<int> rightParts = ParseVersionParts(right);
    const size_t count = std::max(leftParts.size(), rightParts.size());
    for (size_t i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts[i] : 0;
        const int rightValue = i < rightParts.size() ? rightParts[i] : 0;
        if (leftValue != rightValue) {
            return leftValue > rightValue;
        }
    }
    return false;
}

bool IsHardwareScanDirectory(const FilePath& path) {
    return FileExists(path / "LenovoHardwareScanAddin.dll") && FileExists(path / "LdeApi.Client.dll") &&
           FileExists(path / "LdeApi.Server.exe") && FileExists(path / "Lenovo.Vantage.RpcClient.dll");
}

std::optional<FilePath> FindInstalledLenovoHardwareScanDirectory() {
    const std::optional<FilePath> programData = ProgramDataDirectory();
    if (!programData.has_value()) {
        return std::nullopt;
    }

    const FilePath addinRoot = *programData / "Lenovo" / "Vantage" / "Addins" / "LenovoHardwareScanAddin";
    if (!DirectoryExists(addinRoot)) {
        return std::nullopt;
    }

    const std::wstring pattern = (addinRoot / "*").Wide();
    WIN32_FIND_DATAW findData{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<FilePath> bestPath;
    std::wstring bestVersion;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        const std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const FilePath candidate = addinRoot / FilePath(name);
        if (!IsHardwareScanDirectory(candidate)) {
            continue;
        }
        if (!bestPath.has_value() || VersionGreater(name, bestVersion)) {
            bestPath = candidate;
            bestVersion = name;
        }
    } while (FindNextFileW(search, &findData));

    FindClose(search);
    return bestPath;
}

std::vector<NamedScalarMetric> CreateRawMetrics(
    const std::vector<BoardSensorReading>& readings, ScalarMetricUnit unit) {
    std::vector<NamedScalarMetric> metrics;
    metrics.reserve(readings.size());
    for (const BoardSensorReading& reading : readings) {
        metrics.push_back(NamedScalarMetric{reading.title, ScalarMetric{reading.value, unit}});
    }
    return metrics;
}

bool HasLogicalName(const std::vector<std::string>& names, const char* value) {
    return std::any_of(
        names.begin(), names.end(), [value](const std::string& name) { return EqualsInsensitive(name, value); });
}

bool HasUnknownTemperatureRequest(const std::vector<std::string>& names) {
    return std::any_of(names.begin(), names.end(), [](const std::string& name) {
        return !EqualsInsensitive(name, "cpu") && !EqualsInsensitive(name, "gpu") && !EqualsInsensitive(name, "disk") &&
               !EqualsInsensitive(name, "storage") && !EqualsInsensitive(name, "motherboard") &&
               !EqualsInsensitive(name, "system") && !EqualsInsensitive(name, "board") &&
               !EqualsInsensitive(name, "battery");
    });
}

LenovoHardwareScanCaptureOptions CaptureOptionsForSettings(const BoardTelemetrySettings& settings) {
    LenovoHardwareScanCaptureOptions options{};
    const std::vector<std::string>& temperatures = settings.requestedTemperatureNames;
    const bool unknownTemperature = HasUnknownTemperatureRequest(temperatures);
    options.includeCpuTemperature = HasLogicalName(temperatures, "cpu") || unknownTemperature;
    options.includeGpuTemperature = HasLogicalName(temperatures, "gpu") || unknownTemperature;
    options.includeStorageTemperature =
        HasLogicalName(temperatures, "disk") || HasLogicalName(temperatures, "storage") || unknownTemperature;
    options.includeMotherboardTemperature = HasLogicalName(temperatures, "motherboard") ||
                                            HasLogicalName(temperatures, "system") ||
                                            HasLogicalName(temperatures, "board") || unknownTemperature;
    options.includeBatteryTemperature = HasLogicalName(temperatures, "battery") || unknownTemperature;
    options.includeFans = !settings.requestedFanNames.empty();
    return options;
}

std::optional<BoardVendorTelemetrySample> QueryServiceBoardSample(std::string& diagnostics) {
    diagnostics.clear();
    const std::wstring pipeName = WideFromUtf8(kFpsServicePipeName);
    if (!WaitNamedPipeW(pipeName.c_str(), kPipeConnectTimeoutMs)) {
        diagnostics = FormatText("CashDash service pipe is unavailable: %s", FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    Handle pipe(CreateFileW(
        pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (pipe.Get() == INVALID_HANDLE_VALUE) {
        diagnostics =
            FormatText("Failed to connect to CashDash service pipe: %s", FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    const std::vector<char> request = BuildBoardSensorsServiceRequest();
    DWORD written = 0;
    if (!WriteFile(pipe.Get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) {
        diagnostics =
            FormatText("Failed to write board sensor service request: %s", FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    std::vector<char> response;
    for (;;) {
        char buffer[kPipeReadChunkBytes]{};
        DWORD read = 0;
        if (!ReadFile(pipe.Get(), buffer, static_cast<DWORD>(std::size(buffer)), &read, nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                break;
            }
            diagnostics =
                FormatText("Failed to read board sensor service response: %s", FormatWin32Error(error).c_str());
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        if (response.size() + read > kMaximumPipeResponseBytes) {
            diagnostics = "Board sensor service response is too large.";
            return std::nullopt;
        }
        response.insert(response.end(), buffer, buffer + read);
    }

    return ParseBoardSensorsServiceResponse(response.data(), response.size(), diagnostics);
}

LenovoHardwareScanSnapshot SnapshotFromServiceSample(const BoardVendorTelemetrySample& sample) {
    LenovoHardwareScanSnapshot snapshot;
    snapshot.success = sample.available;
    snapshot.diagnostics =
        sample.diagnostics.empty() ? "Lenovo Hardware Scan service sample completed." : sample.diagnostics;
    for (const NamedScalarMetric& metric : sample.fans) {
        snapshot.fans.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    for (const NamedScalarMetric& metric : sample.temperatures) {
        snapshot.temperatures.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    return snapshot;
}

class LenovoHardwareScanCapture final : public LenovoHardwareScanCaptureSink {
public:
    explicit LenovoHardwareScanCapture(Trace& trace) : trace_(trace) {}

    void AddFanReading(const wchar_t* title, double rpm) override {
        snapshot_.fans.push_back(BoardSensorReading{Utf8FromNullableWide(title), rpm});
    }

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(BoardSensorReading{Utf8FromNullableWide(title), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = Utf8FromNullableWide(diagnostics);
    }

    void TraceAssemblyLoaded(const wchar_t* path) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string pathText = Utf8FromNullableWide(path);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("assembly_loaded path=\"%s\""), pathText.c_str());
        }
    }

    void TraceClientStatus(const wchar_t* status) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string statusText = Utf8FromNullableWide(status);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("client_status status=\"%s\""), statusText.c_str());
        }
    }

    void TraceExecutionResult(const wchar_t* result) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string resultText = Utf8FromNullableWide(result);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("execution_result result=\"%s\""), resultText.c_str());
        }
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string diagnosticsText = Utf8FromNullableWide(diagnostics);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("initialize_exception %s"), diagnosticsText.c_str());
        }
    }

    void TraceModuleLoadResult(const wchar_t* result) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string resultText = Utf8FromNullableWide(result);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("module_load_result %s"), resultText.c_str());
        }
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string diagnosticsText = Utf8FromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("snapshot_exception %s"), diagnosticsText.c_str());
        }
    }

    LenovoHardwareScanSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics = FormatText("Lenovo Hardware Scan query completed. fan_count=%zu temp_count=%zu",
            snapshot_.fans.size(),
            snapshot_.temperatures.size());
        const std::vector<std::string> fanNames = ExtractBoardSensorNames(snapshot_.fans);
        const std::vector<std::string> temperatureNames = ExtractBoardSensorNames(snapshot_.temperatures);
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("snapshot_done fan_count=%zu temp_count=%zu fan_names=\"%s\" temp_names=\"%s\""),
            snapshot_.fans.size(),
            snapshot_.temperatures.size(),
            JoinNames(fanNames).c_str(),
            JoinNames(temperatureNames).c_str());
        return std::move(snapshot_);
    }

    LenovoHardwareScanSnapshot FinishFailure() {
        return std::move(snapshot_);
    }

private:
    Trace& trace_;
    LenovoHardwareScanSnapshot snapshot_;
};

LenovoHardwareScanSnapshot CaptureLenovoHardwareScanSensors(Trace& trace,
    LenovoHardwareScanRuntime& runtime,
    const FilePath& addinDirectory,
    const LenovoHardwareScanCaptureOptions& options) {
    LenovoHardwareScanCapture capture(trace);
    const std::wstring wideAddinDirectory = addinDirectory.Wide();
    const bool captured = runtime.Capture(wideAddinDirectory.c_str(), options, capture);
    return captured ? capture.FinishSuccess() : capture.FinishFailure();
}

BoardVendorTelemetrySample CreateRawLenovoSampleFromSnapshot(
    const BoardVendorInfo& info, const LenovoHardwareScanSnapshot& snapshot) {
    BoardVendorTelemetrySample sample;
    sample.providerName = kLenovoProviderName;
    sample.boardManufacturer = info.manufacturer;
    sample.boardProduct = info.product;
    sample.driverLibrary = kLenovoDriverLibrary;
    sample.diagnostics = snapshot.diagnostics;
    sample.availableFanNames = ExtractBoardSensorNames(snapshot.fans);
    sample.availableTemperatureNames = ExtractBoardSensorNames(snapshot.temperatures);
    sample.fans = CreateRawMetrics(snapshot.fans, ScalarMetricUnit::Rpm);
    sample.temperatures = CreateRawMetrics(snapshot.temperatures, ScalarMetricUnit::Celsius);
    sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
    return sample;
}

class LenovoHardwareScanBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    LenovoHardwareScanBoardTelemetryProvider(Trace& trace, BoardVendorInfo info)
        : trace_(trace), info_(std::move(info)) {}

    ~LenovoHardwareScanBoardTelemetryProvider() override {
        if (pendingDirectSnapshot_.valid()) {
            pendingDirectSnapshot_.wait();
        }
    }

    bool Initialize(const BoardTelemetrySettings& settings) override {
        if (pendingDirectSnapshot_.valid()) {
            cachedDirectSnapshot_ = pendingDirectSnapshot_.get();
            hasCachedDirectSnapshot_ = cachedDirectSnapshot_.success;
        }

        settings_ = settings;
        captureOptions_ = CaptureOptionsForSettings(settings_);
        trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("initialize_begin"));

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("board manufacturer=\"%s\" product=\"%s\""),
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Lenovo) {
            diagnostics_ = "Baseboard manufacturer is not Lenovo.";
            return false;
        }

        hardwareScanDirectory_ = FindInstalledLenovoHardwareScanDirectory();
        if (!hardwareScanDirectory_.has_value()) {
            diagnostics_ = "Lenovo Hardware Scan addin directory was not found.";
            return false;
        }

        driverLibrary_ = (*hardwareScanDirectory_ / "LenovoHardwareScanAddin.dll").string();
        diagnostics_ = "Lenovo Hardware Scan provider ready.";
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(
                requestedFanIndexBySourceName_, ResolveFanSensorName(fanMetricTemplate_[i].name), i);
        }

        requestedDiagnosticsSuffix_.clear();
        if (!settings_.requestedTemperatureNames.empty()) {
            AppendFormat(requestedDiagnosticsSuffix_,
                " requested_temps=%s",
                JoinNames(settings_.requestedTemperatureNames).c_str());
        }
        if (!settings_.requestedFanNames.empty()) {
            AppendFormat(
                requestedDiagnosticsSuffix_, " requested_fans=%s", JoinNames(settings_.requestedFanNames).c_str());
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample = CreateBaseSample();
        if (!initialized_ || !hardwareScanDirectory_.has_value()) {
            return sample;
        }

        std::string serviceDiagnostics;
        LenovoHardwareScanSnapshot serviceSnapshot = CaptureServiceSnapshot(serviceDiagnostics);
        if (serviceSnapshot.success) {
            ApplySnapshotToSample(serviceSnapshot, sample);
            return sample;
        }

        std::string directDiagnostics;
        CompletePendingDirectSnapshot(directDiagnostics);
        MaybeStartDirectSnapshot(directDiagnostics);

        if (hasCachedDirectSnapshot_) {
            ApplySnapshotToSample(cachedDirectSnapshot_, sample);
            if (pendingDirectSnapshot_.valid()) {
                sample.diagnostics = FormatText(
                    "%s refresh=running service=\"%s\"", sample.diagnostics.c_str(), serviceDiagnostics.c_str());
            }
            return sample;
        }

        if (directDiagnostics.empty()) {
            directDiagnostics = "Direct Lenovo Hardware Scan refresh is waiting.";
        }
        diagnostics_ = FormatText("Lenovo Hardware Scan unavailable. service=\"%s\" direct=\"%s\"",
            serviceDiagnostics.c_str(),
            directDiagnostics.c_str());
        sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

private:
    BoardVendorTelemetrySample CreateBaseSample() const {
        BoardVendorTelemetrySample sample;
        sample.providerName = kLenovoProviderName;
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = driverLibrary_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

    void ApplySnapshotToSample(const LenovoHardwareScanSnapshot& snapshot, BoardVendorTelemetrySample& sample) {
        diagnostics_ = snapshot.diagnostics;
        availableFanNames_ = ExtractBoardSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractBoardSensorNames(snapshot.temperatures);
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.temperatures);
        ResetBoardMetricValues(sample.fans);
        ApplyBoardSensorReadingsToMetrics(
            snapshot.temperatures, requestedTemperatureIndexBySourceName_, sample.temperatures);
        ApplyBoardSensorReadingsToMetrics(snapshot.fans, requestedFanIndexBySourceName_, sample.fans);
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
    }

    LenovoHardwareScanSnapshot CaptureServiceSnapshot(std::string& diagnostics) {
        if (!serviceUsable_) {
            ++serviceRetrySample_;
            if (serviceRetrySample_ < kSensorRetrySampleInterval) {
                diagnostics = "CashDash service Lenovo Hardware Scan path is waiting for retry.";
                return {};
            }
            serviceRetrySample_ = 0;
        }

        std::optional<BoardVendorTelemetrySample> serviceSample = QueryServiceBoardSample(diagnostics);
        if (!serviceSample.has_value()) {
            serviceUsable_ = false;
            serviceRetrySample_ = 0;
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
                RES_STR("service_sample_failed diagnostics=\"%s\""),
                diagnostics.c_str());
            return {};
        }

        serviceUsable_ = true;
        serviceRetrySample_ = kSensorRetrySampleInterval;
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("service_sample_done available=%d diagnostics=\"%s\""),
            serviceSample->available ? 1 : 0,
            serviceSample->diagnostics.c_str());
        return SnapshotFromServiceSample(*serviceSample);
    }

    void CompletePendingDirectSnapshot(std::string& diagnostics) {
        if (!pendingDirectSnapshot_.valid()) {
            return;
        }
        if (pendingDirectSnapshot_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            diagnostics = "Direct Lenovo Hardware Scan refresh is running.";
            return;
        }

        LenovoHardwareScanSnapshot snapshot = pendingDirectSnapshot_.get();
        diagnostics = snapshot.diagnostics;
        if (snapshot.success) {
            cachedDirectSnapshot_ = std::move(snapshot);
            hasCachedDirectSnapshot_ = true;
        } else if (!hasCachedDirectSnapshot_) {
            diagnostics_ = snapshot.diagnostics;
        }
    }

    void MaybeStartDirectSnapshot(std::string& diagnostics) {
        if (pendingDirectSnapshot_.valid() || !hardwareScanDirectory_.has_value()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (lastDirectSnapshotStart_.has_value() && now - *lastDirectSnapshotStart_ < kDirectSnapshotRefreshInterval) {
            if (diagnostics.empty()) {
                diagnostics = "Direct Lenovo Hardware Scan refresh is waiting.";
            }
            return;
        }

        const FilePath addinDirectory = *hardwareScanDirectory_;
        const LenovoHardwareScanCaptureOptions options = captureOptions_;
        lastDirectSnapshotStart_ = now;
        diagnostics = "Direct Lenovo Hardware Scan refresh started.";
        trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("direct_snapshot_refresh_started"));
        pendingDirectSnapshot_ = std::async(std::launch::async, [this, addinDirectory, options]() {
            return CaptureLenovoHardwareScanSensors(trace_, runtime_, addinDirectory, options);
        });
    }

    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.fanSensorNames, logicalName);
    }

    Trace& trace_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    LenovoHardwareScanRuntime runtime_;
    LenovoHardwareScanCaptureOptions captureOptions_{};
    std::optional<FilePath> hardwareScanDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string driverLibrary_;
    std::string diagnostics_ = "Lenovo Hardware Scan provider not initialized.";
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    BoardMetricIndexBySourceName requestedFanIndexBySourceName_;
    BoardMetricIndexBySourceName requestedTemperatureIndexBySourceName_;
    LenovoHardwareScanSnapshot cachedDirectSnapshot_;
    std::future<LenovoHardwareScanSnapshot> pendingDirectSnapshot_;
    std::optional<std::chrono::steady_clock::time_point> lastDirectSnapshotStart_;
    int serviceRetrySample_ = kSensorRetrySampleInterval;
    bool serviceUsable_ = true;
    bool hasCachedDirectSnapshot_ = false;
    bool initialized_ = false;
};

}  // namespace

BoardVendorTelemetrySample CaptureLenovoHardwareScanServiceSample(Trace& trace, BoardVendorInfo info) {
    if (SelectBoardVendor(info) != BoardVendor::Lenovo) {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Unsupported";
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.diagnostics = "No Lenovo Hardware Scan provider matches the baseboard manufacturer.";
        return sample;
    }

    const std::optional<FilePath> addinDirectory = FindInstalledLenovoHardwareScanDirectory();
    if (!addinDirectory.has_value()) {
        BoardVendorTelemetrySample sample;
        sample.providerName = kLenovoProviderName;
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.driverLibrary = kLenovoDriverLibrary;
        sample.diagnostics = "Lenovo Hardware Scan addin directory was not found.";
        return sample;
    }

    LenovoHardwareScanRuntime runtime;
    LenovoHardwareScanCaptureOptions options;
    LenovoHardwareScanSnapshot snapshot = CaptureLenovoHardwareScanSensors(trace, runtime, *addinDirectory, options);
    return CreateRawLenovoSampleFromSnapshot(info, snapshot);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateLenovoBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return std::make_unique<LenovoHardwareScanBoardTelemetryProvider>(trace, std::move(info));
}
