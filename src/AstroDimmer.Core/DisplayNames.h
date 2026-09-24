#pragma once

#include <optional>
#include <string>

/// Human-facing facts about a display, worked out from strings Windows and
/// the monitor already hand over.
///
/// The raw device key ("\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642") is a
/// troubleshooting string, not something a person can match to the monitor
/// in front of them. What helps is the maker, the display number Windows
/// shows in its own Settings, and the model.
namespace AstroDimmer::Core::DisplayNames
{
    /// The maker from a device key such as "\\?\DISPLAY#DEL436E#...", where
    /// DEL is the PnP vendor ID. Unknown codes come back as the code itself,
    /// which is still more recognisable than the path.
    std::optional<std::wstring> Vendor(std::wstring const& deviceKey);

    /// "\\.\DISPLAY2" -> 2: the number Windows shows in its display settings.
    std::optional<int> DisplayNumber(std::wstring const& deviceName);

    /// Names Windows gives a monitor it has no real name for. They are worse
    /// than a model number read over DDC, so callers should prefer that.
    bool IsGenericName(std::wstring const& name);

    /// The model(...) field of an MCCS capability string, if it has one.
    std::optional<std::wstring> ModelFromCapabilities(std::wstring const& capabilities);

    /// The name to show, best source first: Windows' own friendly name, then
    /// the model read off the wire, then maker and display number.
    ///
    /// displayFormat is "Display {0}" in the user's language; this library
    /// has no translations of its own, so the app passes it in.
    std::wstring Resolve(std::optional<std::wstring> const& friendlyName,
                         std::wstring const& capabilities, std::wstring const& deviceName,
                         std::wstring const& deviceKey, std::wstring const& displayFormat = L"Display {0}");
}
