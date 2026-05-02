#include "telemetry/gpu/gpu_vendor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"
#include "util/trace.h"

namespace {

class FirstAvailableGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    explicit FirstAvailableGpuTelemetryProvider(std::vector<std::unique_ptr<GpuVendorTelemetryProvider>> providers)
        : providers_(std::move(providers)) {}

    bool Initialize() override {
        std::string failures;
        for (auto& provider : providers_) {
            if (provider == nullptr) {
                continue;
            }
            if (provider->Initialize()) {
                activeProvider_ = std::move(provider);
                providers_.clear();
                return true;
            }

            const GpuVendorTelemetrySample sample = provider->Sample();
            if (!failures.empty()) {
                failures += "; ";
            }
            failures += sample.providerName + ": " + sample.diagnostics;
        }

        diagnostics_ = failures.empty() ? "No GPU vendor providers were available." : failures;
        return false;
    }

    GpuVendorTelemetrySample Sample() override {
        if (activeProvider_ != nullptr) {
            return activeProvider_->Sample();
        }

        GpuVendorTelemetrySample sample;
        sample.providerName = "GPU vendor";
        sample.diagnostics = diagnostics_;
        sample.available = false;
        return sample;
    }

private:
    std::vector<std::unique_ptr<GpuVendorTelemetryProvider>> providers_;
    std::unique_ptr<GpuVendorTelemetryProvider> activeProvider_;
    std::string diagnostics_ = "GPU vendor provider not initialized.";
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace) {
    std::vector<std::unique_ptr<GpuVendorTelemetryProvider>> providers;
    providers.push_back(CreateNvidiaGpuTelemetryProvider(trace));
    providers.push_back(CreateAmdGpuTelemetryProvider(trace));
    return std::make_unique<FirstAvailableGpuTelemetryProvider>(std::move(providers));
}
