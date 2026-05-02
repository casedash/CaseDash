#include "telemetry/board/board_vendor.h"

#include "telemetry/board/gigabyte/board_gigabyte_siv.h"
#include "telemetry/board/msi/board_msi_center.h"
#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(Trace& trace) {
    const std::string manufacturer =
        ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
    if (ContainsInsensitive(manufacturer, "micro-star") || ContainsInsensitive(manufacturer, "msi")) {
        return CreateMsiBoardTelemetryProvider(trace);
    }
    if (ContainsInsensitive(manufacturer, "gigabyte")) {
        return CreateGigabyteBoardTelemetryProvider(trace);
    }

    return CreateGigabyteBoardTelemetryProvider(trace);
}
