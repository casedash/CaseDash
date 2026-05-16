#include "telemetry/gpu/amd/gpu_amd_adl.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"
#include "vendor/adlx/SDK/Include/ISystem.h"

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using namespace adlx;

std::string AdlxResultCodeString(ADLX_RESULT result) {
    return std::to_string(static_cast<int>(result));
}

std::string AdlxString(const char* value) {
    return value != nullptr && value[0] != '\0' ? Utf8FromAnsi(value) : std::string();
}

std::optional<unsigned int> ParseHexText(std::string text) {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    return end != nullptr && *end == '\0' ? std::optional<unsigned int>{static_cast<unsigned int>(value)}
                                          : std::nullopt;
}

std::optional<unsigned int> ParsePnpHexField(const std::string& text, const char* key) {
    const size_t pos = text.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t start = pos + std::string(key).size();
    size_t end = start;
    while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || (text[end] >= 'a' && text[end] <= 'f') ||
                                    (text[end] >= 'A' && text[end] <= 'F'))) {
        ++end;
    }
    return ParseHexText(text.substr(start, end - start));
}

struct AdlxGpuIdentity {
    std::string name;
    std::string pnpString;
    unsigned int deviceId = 0;
    unsigned int vendorId = 0;
    unsigned int subSysId = 0;
    unsigned int revision = 0;
};

AdlxGpuIdentity ReadAdlxGpuIdentity(IADLXGPUPtr gpu) {
    AdlxGpuIdentity identity;
    const char* text = nullptr;
    if (gpu->Name(&text) == ADLX_OK) {
        identity.name = AdlxString(text);
    }
    if (gpu->PNPString(&text) == ADLX_OK) {
        identity.pnpString = AdlxString(text);
    }
    if (gpu->DeviceId(&text) == ADLX_OK) {
        identity.deviceId = ParseHexText(AdlxString(text)).value_or(0);
    }
    if (gpu->VendorId(&text) == ADLX_OK) {
        identity.vendorId = ParseHexText(AdlxString(text)).value_or(0);
    }

    if (!identity.pnpString.empty()) {
        identity.vendorId = ParsePnpHexField(identity.pnpString, "VEN_").value_or(identity.vendorId);
        identity.deviceId = ParsePnpHexField(identity.pnpString, "DEV_").value_or(identity.deviceId);
        identity.subSysId = ParsePnpHexField(identity.pnpString, "SUBSYS_").value_or(identity.subSysId);
        identity.revision = ParsePnpHexField(identity.pnpString, "REV_").value_or(identity.revision);
    }
    return identity;
}

int AmdDeviceMatchRank(const GpuVendorInfo& adapter, const AdlxGpuIdentity& identity) {
    if (identity.vendorId == adapter.vendorId && identity.deviceId == adapter.deviceId &&
        (adapter.subSysId == 0 || identity.subSysId == adapter.subSysId) &&
        (adapter.revision == 0 || identity.revision == adapter.revision)) {
        return 4;
    }
    if (identity.vendorId == adapter.vendorId && identity.deviceId == adapter.deviceId) {
        return 3;
    }
    if (!adapter.adapterName.empty()) {
        if (EqualsInsensitive(identity.name, adapter.adapterName)) {
            return 2;
        }
        if (ContainsInsensitive(identity.name, adapter.adapterName) ||
            ContainsInsensitive(adapter.adapterName, identity.name)) {
            return 1;
        }
    }
    return 0;
}

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    AmdAdlxGpuTelemetryProvider(Trace& trace, std::optional<GpuVendorInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        trace().Write(TracePrefix::AmdAdlx, "initialize_begin");
        ADLX_RESULT result = helper_.Initialize();
        trace().Write(TracePrefix::AmdAdlx, "helper_initialize result=" + AdlxResultCodeString(result));
        if (ADLX_FAILED(result)) {
            trace().Write(
                TracePrefix::AmdAdlx, "helper_initialize_incompatible_begin result=" + AdlxResultCodeString(result));
            result = helper_.InitializeWithIncompatibleDriver();
            trace().Write(
                TracePrefix::AmdAdlx, "helper_initialize_incompatible_done result=" + AdlxResultCodeString(result));
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ = "ADLX initialization failed: init=" + AdlxResultCodeString(result);
            trace().Write(TracePrefix::AmdAdlx, "initialize_failed " + diagnostics_);
            return false;
        }

        trace().Write(TracePrefix::AmdAdlx, "get_performance_monitoring_begin");
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        trace().Write(TracePrefix::AmdAdlx,
            "get_performance_monitoring_done result=" + AdlxResultCodeString(result) +
                " available=" + Trace::BoolText(performanceMonitoring_ != nullptr));
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = "Failed to get ADLX performance monitoring services: perf=" + AdlxResultCodeString(result);
            trace().Write(TracePrefix::AmdAdlx, "get_performance_monitoring_failed " + diagnostics_);
            return false;
        }

        IADLXGPUListPtr gpus;
        trace().Write(TracePrefix::AmdAdlx, "get_gpus_begin");
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        trace().Write(TracePrefix::AmdAdlx,
            "get_gpus_done result=" + AdlxResultCodeString(result) + " available=" + Trace::BoolText(gpus != nullptr));
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ = "Failed to get AMD GPU list: gpus=" + AdlxResultCodeString(result);
            trace().Write(TracePrefix::AmdAdlx, "get_gpus_failed " + diagnostics_);
            return false;
        }

        if (!SelectGpu(gpus)) {
            return false;
        }

        if (gpuName_.empty()) {
            gpuName_ = "AMD GPU";
        }

        adlx_uint totalVramMb = 0;
        const ADLX_RESULT totalVramResult = gpu_->TotalVRAM(&totalVramMb);
        trace().Write(TracePrefix::AmdAdlx,
            "get_total_vram result=" + AdlxResultCodeString(totalVramResult) + " mb=" + std::to_string(totalVramMb));
        if (ADLX_SUCCEEDED(totalVramResult) && totalVramMb > 0) {
            totalVramGb_ = static_cast<double>(totalVramMb) / 1024.0;
        }

        trace().Write(TracePrefix::AmdAdlx, "get_supported_metrics_begin");
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        trace().Write(TracePrefix::AmdAdlx,
            "get_supported_metrics_done result=" + AdlxResultCodeString(result) +
                " available=" + Trace::BoolText(metricsSupport_ != nullptr));
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ = "Failed to query supported AMD GPU metrics: support=" + AdlxResultCodeString(result);
            trace().Write(TracePrefix::AmdAdlx, "get_supported_metrics_failed " + diagnostics_);
            return false;
        }

        adlx_bool tempSupported = false;
        adlx_bool usageSupported = false;
        adlx_bool clockSupported = false;
        adlx_bool fanSupported = false;
        adlx_bool vramSupported = false;
        const ADLX_RESULT usageResult = metricsSupport_->IsSupportedGPUUsage(&usageSupported);
        const ADLX_RESULT tempResult = metricsSupport_->IsSupportedGPUTemperature(&tempSupported);
        const ADLX_RESULT clockResult = metricsSupport_->IsSupportedGPUClockSpeed(&clockSupported);
        const ADLX_RESULT fanResult = metricsSupport_->IsSupportedGPUFanSpeed(&fanSupported);
        const ADLX_RESULT vramResult = metricsSupport_->IsSupportedGPUVRAM(&vramSupported);
        usageSupported_ = ADLX_SUCCEEDED(usageResult) && usageSupported;
        temperatureSupported_ = ADLX_SUCCEEDED(tempResult) && tempSupported;
        clockSupported_ = ADLX_SUCCEEDED(clockResult) && clockSupported;
        fanSupported_ = ADLX_SUCCEEDED(fanResult) && fanSupported;
        vramSupported_ = ADLX_SUCCEEDED(vramResult) && vramSupported;

        diagnostics_ =
            "ADLX GPU=" + gpuName_ + " usage_supported=" + (usageSupported ? "yes" : "no") + "(" +
            std::to_string(static_cast<int>(usageResult)) + ")" + " temp_supported=" + (tempSupported ? "yes" : "no") +
            "(" + std::to_string(static_cast<int>(tempResult)) + ")" +
            " clock_supported=" + (clockSupported ? "yes" : "no") + "(" +
            std::to_string(static_cast<int>(clockResult)) + ")" + " fan_supported=" + (fanSupported ? "yes" : "no") +
            "(" + std::to_string(static_cast<int>(fanResult)) + ")" +
            " vram_supported=" + (vramSupported ? "yes" : "no") + "(" + std::to_string(static_cast<int>(vramResult)) +
            ")";
        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }
        initialized_ = true;
        trace().Write(TracePrefix::AmdAdlx,
            "initialize_done diagnostics=\"" + diagnostics_ + "\" fps=\"" + fpsDiagnostics_ + "\"");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace().Write(TracePrefix::AmdAdlx, "sample_begin");
        GpuVendorTelemetrySample sample;
        sample.providerName = "AMD ADLX";
        sample.name = gpuName_;
        sample.totalVramGb = totalVramGb_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || !performanceMonitoring_ || !gpu_ || !metricsSupport_) {
            sample.available = false;
            return sample;
        }

        IADLXGPUMetricsPtr metrics;
        trace().Write(TracePrefix::AmdAdlx, "get_current_metrics_begin");
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
            return "get_current_metrics_done result=" + AdlxResultCodeString(metricsResult) +
                   " available=" + Trace::BoolText(metrics != nullptr);
        });
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics = diagnostics_ + " current_metrics=" + AdlxResultCodeString(metricsResult);
            sample.available = false;
            trace().WriteLazy(TracePrefix::AmdAdlx,
                [&] { return "get_current_metrics_failed diagnostics=\"" + sample.diagnostics + "\""; });
            return sample;
        }
        bool hasAnyMetric = false;

        if (usageSupported_) {
            adlx_double usage = 0.0;
            trace().Write(TracePrefix::AmdAdlx, "get_usage_begin");
            const ADLX_RESULT result = metrics->GPUUsage(&usage);
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                char buffer[128];
                sprintf_s(buffer, "get_usage_done result=%d value=%.1f", static_cast<int>(result), usage);
                return std::string(buffer);
            });
            if (ADLX_SUCCEEDED(result)) {
                sample.loadPercent = usage;
                hasAnyMetric = true;
            }
        }

        if (temperatureSupported_) {
            adlx_double temperature = 0.0;
            trace().Write(TracePrefix::AmdAdlx, "get_temperature_begin");
            const ADLX_RESULT result = metrics->GPUTemperature(&temperature);
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                char buffer[128];
                sprintf_s(buffer, "get_temperature_done result=%d value=%.1f", static_cast<int>(result), temperature);
                return std::string(buffer);
            });
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        if (clockSupported_) {
            adlx_int clockMhz = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_clock_begin");
            const ADLX_RESULT result = metrics->GPUClockSpeed(&clockMhz);
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                char buffer[128];
                sprintf_s(
                    buffer, "get_clock_done result=%d value=%d", static_cast<int>(result), static_cast<int>(clockMhz));
                return std::string(buffer);
            });
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        if (fanSupported_) {
            adlx_int fanRpm = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_fan_begin");
            const ADLX_RESULT result = metrics->GPUFanSpeed(&fanRpm);
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                char buffer[128];
                sprintf_s(
                    buffer, "get_fan_done result=%d value=%d", static_cast<int>(result), static_cast<int>(fanRpm));
                return std::string(buffer);
            });
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        if (vramSupported_) {
            adlx_int usedVramMb = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_vram_begin");
            const ADLX_RESULT result = metrics->GPUVRAM(&usedVramMb);
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                char buffer[128];
                sprintf_s(
                    buffer, "get_vram_done result=%d value=%d", static_cast<int>(result), static_cast<int>(usedVramMb));
                return std::string(buffer);
            });
            if (ADLX_SUCCEEDED(result) && usedVramMb >= 0) {
                sample.usedVramGb = static_cast<double>(usedVramMb) / 1024.0;
                hasAnyMetric = true;
            }
        }

        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsAppName = fpsSample.processName;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                hasAnyMetric = true;
            } else if (fpsSample.permissionRequired) {
                const std::optional<double> nativeFps = ReadNativeAmdFps();
                if (nativeFps.has_value()) {
                    sample.fps = *nativeFps;
                    sample.fpsAppName = "!admin";
                    hasAnyMetric = true;
                }
            }
            trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
                return std::string("get_presented_fps available=") + Trace::BoolText(fpsSample.fps.has_value()) +
                       " permission_required=" + Trace::BoolText(fpsSample.permissionRequired) + " value=" +
                       (fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1)
                                                  : std::string("fps=N/A")) +
                       " process=\"" + fpsSample.processName + "\" diagnostics=\"" + fpsSample.diagnostics + "\"";
            });
        }

        sample.available = hasAnyMetric;
        sample.diagnostics += " fps=" + fpsDiagnostics_;
        trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
            return std::string("sample_done available=") + Trace::BoolText(sample.available) + " diagnostics=\"" +
                   sample.diagnostics + "\"";
        });
        return sample;
    }

private:
    bool SelectGpu(const IADLXGPUListPtr& gpus) {
        int bestRank = -1;
        IADLXGPUPtr bestGpu;
        std::string bestName;
        std::string bestMatch = "fallback";
        ADLX_RESULT bestResult = ADLX_FAIL;

        for (adlx_uint index = gpus->Begin(); index < gpus->End(); ++index) {
            IADLXGPUPtr candidate;
            const ADLX_RESULT result = gpus->At(index, &candidate);
            const AdlxGpuIdentity identity = candidate != nullptr ? ReadAdlxGpuIdentity(candidate) : AdlxGpuIdentity{};
            const int rank = candidate != nullptr && adapter_.has_value() ? AmdDeviceMatchRank(*adapter_, identity)
                                                                          : (candidate != nullptr ? 0 : -1);
            trace().Write(TracePrefix::AmdAdlx,
                "gpu_candidate index=" + std::to_string(index) + " result=" + AdlxResultCodeString(result) +
                    " vendor_id=0x" + HexId(identity.vendorId, 4) + " device_id=0x" + HexId(identity.deviceId, 4) +
                    " subsystem_id=0x" + HexId(identity.subSysId, 8) + " revision=0x" + HexId(identity.revision, 2) +
                    " match_rank=" + std::to_string(rank) + " name=\"" + identity.name + "\" pnp=\"" +
                    identity.pnpString + "\"");
            if (candidate != nullptr && rank > bestRank) {
                bestRank = rank;
                bestGpu = candidate;
                bestName = identity.name;
                bestResult = result;
                bestMatch = rank >= 3 ? "device_id" : (rank >= 1 ? "name" : "fallback");
            }
        }

        gpu_ = bestGpu;
        gpuName_ = bestMatch == "device_id" && adapter_.has_value() && !adapter_->adapterName.empty()
                       ? adapter_->adapterName
                       : bestName;
        trace().Write(TracePrefix::AmdAdlx,
            "gpu_selected match=\"" + bestMatch + "\" rank=" + std::to_string(bestRank) + " display_name=\"" +
                gpuName_ + "\" selected_adapter=\"" + (adapter_.has_value() ? adapter_->adapterName : std::string()) +
                "\"");
        if (!gpu_) {
            diagnostics_ = "Failed to open selected AMD GPU: gpu=" + AdlxResultCodeString(bestResult);
            trace().Write(TracePrefix::AmdAdlx, "get_gpu_failed " + diagnostics_);
            return false;
        }
        return true;
    }

    static std::string HexId(unsigned int value, int width) {
        char buffer[16];
        sprintf_s(buffer, "%0*X", width, value);
        return buffer;
    }

    std::optional<double> ReadNativeAmdFps() {
        IADLXFPSPtr fpsMetric;
        trace().Write(TracePrefix::AmdAdlx, "get_native_fps_begin");
        const ADLX_RESULT fpsMetricResult = performanceMonitoring_->GetCurrentFPS(&fpsMetric);
        trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
            return "get_native_fps_metric_done result=" + AdlxResultCodeString(fpsMetricResult) +
                   " available=" + Trace::BoolText(fpsMetric != nullptr);
        });
        if (ADLX_FAILED(fpsMetricResult) || !fpsMetric) {
            return std::nullopt;
        }

        adlx_int fps = 0;
        const ADLX_RESULT fpsResult = fpsMetric->FPS(&fps);
        trace().WriteLazy(TracePrefix::AmdAdlx, [&] {
            char buffer[128];
            sprintf_s(
                buffer, "get_native_fps_done result=%d value=%d", static_cast<int>(fpsResult), static_cast<int>(fps));
            return std::string(buffer);
        });
        return ADLX_SUCCEEDED(fpsResult) && fps >= 0 ? std::optional<double>{static_cast<double>(fps)} : std::nullopt;
    }

    Trace& trace() {
        return trace_;
    }

    ADLXHelper helper_;
    IADLXGPUPtr gpu_;
    IADLXPerformanceMonitoringServicesPtr performanceMonitoring_;
    IADLXGPUMetricsSupportPtr metricsSupport_;
    Trace& trace_;
    std::optional<GpuVendorInfo> adapter_;
    std::string gpuName_;
    std::string diagnostics_ = "ADLX provider not initialized.";
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool usageSupported_ = false;
    bool temperatureSupported_ = false;
    bool clockSupported_ = false;
    bool fanSupported_ = false;
    bool vramSupported_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(
    Trace& trace, std::optional<GpuVendorInfo> adapter) {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>(trace, std::move(adapter));
}
