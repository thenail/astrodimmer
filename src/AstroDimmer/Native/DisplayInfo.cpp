#include "pch.h"
#include "Native/DisplayInfo.h"
#include "DisplayNames.h"
#include "Strings.h"

namespace AstroDimmer::Native::DisplayInfo
{
    namespace
    {
        struct PathName
        {
            std::wstring SourceName;
            std::wstring DevicePath;
            std::wstring FriendlyName;
        };

        /// The active paths, described. Nothing at all if any one cannot be.
        std::optional<std::vector<PathName>> QueryPaths()
        {
            std::vector<DISPLAYCONFIG_PATH_INFO> paths;
            std::vector<DISPLAYCONFIG_MODE_INFO> modes;
            LONG status;

            // The topology can grow between sizing and querying - exactly
            // when a refresh is most likely, mid dock or replug. Resize and go
            // again rather than dropping to the weaker fallback.
            do
            {
                UINT32 pathCount = 0, modeCount = 0;
                if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
                    return std::nullopt;

                paths.resize(pathCount);
                modes.resize(modeCount);
                status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                                            modes.data(), nullptr);
                paths.resize(pathCount);
            } while (status == ERROR_INSUFFICIENT_BUFFER);

            if (status != ERROR_SUCCESS)
                return std::nullopt;

            std::vector<PathName> result;
            for (auto const& path : paths)
            {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;

                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;

                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                    DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS ||
                    source.viewGdiDeviceName[0] == L'\0' || target.monitorDevicePath[0] == L'\0')
                    return std::nullopt;

                result.push_back({ source.viewGdiDeviceName, target.monitorDevicePath,
                                   target.monitorFriendlyDeviceName });
            }

            return result;
        }

        HMONITOR FindMonitor(std::wstring const& deviceName)
        {
            for (auto const& m : Monitors())
                if (_wcsicmp(m.SourceName.c_str(), deviceName.c_str()) == 0)
                    return m.Handle;
            return nullptr;
        }
    }

    std::vector<MonitorHandle> Monitors()
    {
        std::vector<MonitorHandle> found;
        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL
            {
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                std::wstring name = GetMonitorInfoW(monitor, &info) ? info.szDevice : L"";
                reinterpret_cast<std::vector<MonitorHandle>*>(data)->push_back({ monitor, name });
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&found));
        return found;
    }

    std::vector<Core::DisplayDeviceEntry> MonitorDevices()
    {
        std::vector<Core::DisplayDeviceEntry> found;

        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        for (DWORD a = 0; EnumDisplayDevicesW(nullptr, a, &adapter, 0); ++a)
        {
            DISPLAY_DEVICEW monitor{};
            monitor.cb = sizeof(monitor);

            // EDD_GET_DEVICE_INTERFACE_NAME makes DeviceID the device interface
            // path rather than the hardware ID - the form the key is taken from.
            for (DWORD m = 0; EnumDisplayDevicesW(adapter.DeviceName, m, &monitor, EDD_GET_DEVICE_INTERFACE_NAME); ++m)
            {
                if (monitor.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
                    found.push_back({ adapter.DeviceName, monitor.DeviceName, monitor.DeviceID });

                monitor = {};
                monitor.cb = sizeof(monitor);
            }

            adapter = {};
            adapter.cb = sizeof(adapter);
        }

        return found;
    }

    std::vector<Core::DisplayTarget> Targets()
    {
        std::vector<Core::DisplayTarget> targets;
        if (auto paths = QueryPaths())
            for (auto& p : *paths)
                targets.push_back({ p.SourceName, p.DevicePath, p.FriendlyName });
        return targets;
    }

    std::optional<std::wstring> FriendlyName(std::wstring const& deviceName)
    {
        auto paths = QueryPaths();
        if (!paths) return std::nullopt;

        for (auto const& p : *paths)
        {
            if (_wcsicmp(p.SourceName.c_str(), deviceName.c_str()) != 0)
                continue;

            if (p.FriendlyName.empty() || Core::DisplayNames::IsGenericName(p.FriendlyName))
                return std::nullopt;

            return p.FriendlyName;
        }

        return std::nullopt;
    }

    std::optional<Mode> CurrentMode(std::wstring const& deviceName)
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &mode))
            return std::nullopt;

        return Mode{ static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight),
                     static_cast<int>(mode.dmDisplayFrequency) };
    }

    std::optional<std::vector<uint8_t>> Edid(std::wstring const& deviceKey)
    {
        // "\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642" names the device node
        // Enum\DISPLAY\DEL436E\4&3dfd0c9&0&UID24642.
        auto model = deviceKey.find(L'#');
        auto instance = model == std::wstring::npos ? model : deviceKey.find(L'#', model + 1);
        if (instance == std::wstring::npos) return std::nullopt;

        auto instanceEnd = deviceKey.find(L'#', instance + 1);
        std::wstring subkey = LR"(SYSTEM\CurrentControlSet\Enum\DISPLAY\)" +
                              deviceKey.substr(model + 1, instance - model - 1) + L"\\" +
                              deviceKey.substr(instance + 1, instanceEnd == std::wstring::npos
                                                                 ? std::wstring::npos : instanceEnd - instance - 1) +
                              LR"(\Device Parameters)";

        DWORD size = 0;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, subkey.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, nullptr, &size) !=
                ERROR_SUCCESS || size == 0)
            return std::nullopt;

        std::vector<uint8_t> edid(size);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, subkey.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, edid.data(), &size) !=
            ERROR_SUCCESS)
            return std::nullopt;

        edid.resize(size);
        return edid;
    }

    std::optional<double> Scale(std::wstring const& deviceName)
    {
        HMONITOR monitor = FindMonitor(deviceName);
        if (!monitor) return std::nullopt;

        UINT dpiX = 0, dpiY = 0;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
            return std::nullopt;

        // 96 DPI is 100% by definition.
        return dpiX / 96.0;
    }

    std::wstring Describe(std::wstring const& deviceKey, std::wstring const& deviceName)
    {
        std::vector<std::wstring> parts;

        if (auto vendor = Core::DisplayNames::Vendor(deviceKey))
            parts.push_back(*vendor);

        if (auto number = Core::DisplayNames::DisplayNumber(deviceName))
            parts.push_back(Strings::Format(L"DisplayNumber", { std::to_wstring(*number) }));

        auto mode = CurrentMode(deviceName);
        if (mode)
            parts.push_back(std::to_wstring(mode->Width) + L" × " + std::to_wstring(mode->Height));

        if (auto scale = Scale(deviceName))
        {
            auto text = std::to_wstring(static_cast<int>(std::lround(*scale * 100))) + L"%";

            // Only worth showing the effective size when scaling changes it.
            if (mode && std::abs(*scale - 1.0) > 0.001)
            {
                text += L" (" + std::to_wstring(std::lround(mode->Width / *scale)) + L" × " +
                        std::to_wstring(std::lround(mode->Height / *scale)) + L")";
            }

            parts.push_back(text);
        }

        std::wstring joined;
        for (auto const& part : parts)
        {
            if (!joined.empty()) joined += L"  ·  ";
            joined += part;
        }
        return joined;
    }

    int SystemMonitorCount()
    {
        return GetSystemMetrics(SM_CMONITORS);
    }
}
