#pragma once

#include <optional>
#include <string>
#include <vector>
#include "MonitorMatcher.h"

/// What Windows knows about the displays attached right now: the handles and
/// names the DDC layer matches against, and the facts used to label a display
/// in the UI.
namespace AstroDimmer::Native::DisplayInfo
{
    struct MonitorHandle
    {
        HMONITOR Handle;

        /// The GDI name, "\\.\DISPLAY2".
        std::wstring SourceName;
    };

    /// Every HMONITOR with its GDI name, in Windows' order.
    std::vector<MonitorHandle> Monitors();

    /// Monitors attached to the desktop, as EnumDisplayDevices lists them.
    std::vector<Core::DisplayDeviceEntry> MonitorDevices();

    /// Every active path's source name, monitor device path and friendly name,
    /// in QueryDisplayConfig's order.
    ///
    /// All or nothing: if any path cannot be described, the list is empty. A
    /// list with a gap would shift the positions of the paths after it, and
    /// position is what MonitorMatcher falls back on - so a partial answer
    /// could tie a monitor to its neighbour's handle.
    std::vector<Core::DisplayTarget> Targets();

    /// The monitor's name as Windows shows it, e.g. "DELL U3225QE" for
    /// "\\.\DISPLAY2"; nothing when Windows has no real name for it.
    std::optional<std::wstring> FriendlyName(std::wstring const& deviceName);

    struct Mode
    {
        int Width;
        int Height;
        int Hz;
    };

    std::optional<Mode> CurrentMode(std::wstring const& deviceName);

    /// The monitor's EDID as Windows stored it when the monitor was installed,
    /// for a device key such as "\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642".
    std::optional<std::vector<uint8_t>> Edid(std::wstring const& deviceKey);

    /// The scale factor Windows applies to the display, e.g. 1.5 for 150%.
    std::optional<double> Scale(std::wstring const& deviceName);

    /// One line to tell displays apart, e.g.
    /// "Dell  ·  Display 2  ·  3840 × 2160  ·  150% (2560 × 1440)".
    ///
    /// Both sizes are shown deliberately: the first is the panel's real
    /// pixels, the second the area apps lay out against. On a scaled display
    /// they differ, and seeing only one of them is what makes scaling confusing.
    std::wstring Describe(std::wstring const& deviceKey, std::wstring const& deviceName);

    /// How many monitors Windows itself reports. Tells "this machine has no
    /// external displays" apart from "DDC/CI failed to find the ones attached".
    int SystemMonitorCount();
}
