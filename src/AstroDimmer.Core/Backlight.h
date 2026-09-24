#pragma once

#include <optional>
#include <string>
#include <vector>

/// The pure half of driving a built-in panel's backlight through WMI.
///
/// A laptop's own screen hangs off the GPU over eDP and has no DDC/CI
/// controller to talk to; Windows exposes its backlight through the
/// WmiMonitorBrightness classes instead. WMI names a panel by its PnP
/// instance and speaks in whole percentages, but many panels accept only a
/// handful of them - so this is where those two facts are turned into what
/// the rest of the app expects.
namespace AstroDimmer::Core::Backlight
{
    /// The device key a WMI instance name refers to:
    /// "DISPLAY\BOE0747\4&2d4ab9b&0&UID8388688_0" becomes
    /// "\\?\DISPLAY#BOE0747#4&2d4ab9b&0&UID8388688". Nothing when the name
    /// is not in that form.
    ///
    /// The casing is WMI's, which need not be the display path's; compare
    /// the result case-insensitively.
    std::optional<std::wstring> KeyOf(std::wstring const& instanceName);

    /// The level a panel will actually settle on when asked for percent: the
    /// nearest one it supports, the higher on a tie. With no levels listed,
    /// the percentage itself.
    int Snap(int percent, std::vector<int> const& levels);
}
