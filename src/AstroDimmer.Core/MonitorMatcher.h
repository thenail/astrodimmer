#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace AstroDimmer::Core
{
    /// An active display path as QueryDisplayConfig reports it: the GDI
    /// source ("\\.\DISPLAY2") driving a monitor, and that monitor's device path.
    struct DisplayTarget
    {
        std::wstring SourceName;
        std::wstring DevicePath;
        std::wstring FriendlyName;

        std::wstring DeviceKey() const;
    };

    /// A monitor device as EnumDisplayDevices reports it, e.g. DeviceName
    /// "\\.\DISPLAY2\Monitor0" under SourceName "\\.\DISPLAY2".
    struct DisplayDeviceEntry
    {
        std::wstring SourceName;
        std::wstring DeviceName;
        std::wstring DeviceId;

        std::wstring DeviceKey() const;
    };

    enum class MatchMethod
    {
        /// The physical monitor's description named exactly one target.
        Description,

        /// Position among the QueryDisplayConfig targets on the source.
        TargetOrder,

        /// Position among the EnumDisplayDevices monitors on the source.
        DeviceOrder,
    };

    wchar_t const* ToString(MatchMethod method);

    struct MonitorIdentity
    {
        /// Stable identity. Settings are keyed by it, so its format must
        /// never change - it is Glimmer's, byte for byte.
        std::wstring DeviceKey;
        std::wstring DevicePath;

        /// The monitor's DISPLAY_DEVICE name; also the order monitors are
        /// listed in.
        std::wstring DeviceName;

        MatchMethod Method;
    };

    /// Works out which monitor a physical-monitor handle talks to.
    ///
    /// dxva2 hands out physical monitors per HMONITOR, by index, with only a
    /// free-text description. Nothing in Windows documents how that index
    /// relates to the device paths QueryDisplayConfig or EnumDisplayDevices
    /// report - and the device path is the only identity that survives a
    /// replug or reboot. So the link is inferred, strongest evidence first:
    ///
    ///   1. Description, when it is unique on its HMONITOR and equals the
    ///      friendly name of exactly one target on the same source.
    ///   2. Target order: the Nth target on the source.
    ///   3. Device order: the Nth monitor EnumDisplayDevices lists under it.
    ///
    /// Each device key is handed out at most once per refresh: when evidence
    /// runs out, a monitor goes unmatched rather than two handles aliasing
    /// one monitor, where a brightness change would land on the wrong screen.
    ///
    /// Stateful across one refresh; make a new one per refresh and feed it
    /// monitors in enumeration order.
    class MonitorMatcher
    {
    public:
        MonitorMatcher(std::vector<DisplayTarget> targets, std::vector<DisplayDeviceEntry> devices);

        /// A device path minus its interface-class suffix:
        /// "\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642#{e6f07b5f-...}" becomes
        /// "\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642".
        static std::wstring KeyOf(std::wstring const& devicePath);

        /// sourceName is the HMONITOR's GDI name; descriptions are every
        /// physical monitor on that HMONITOR in dxva2's order, and index says
        /// which of them to match. Nothing when no key can be claimed for it.
        std::optional<MonitorIdentity> Match(std::wstring const& sourceName,
                                             std::vector<std::wstring> const& descriptions, size_t index);

    private:
        std::optional<MonitorIdentity> ByDescription(std::vector<DisplayTarget const*> const& onSource,
                                                     std::vector<std::wstring> const& descriptions, size_t index);
        std::optional<MonitorIdentity> ByTargetOrder(std::vector<DisplayTarget const*> const& onSource, size_t index);
        std::optional<MonitorIdentity> ByDeviceOrder(std::wstring const& sourceName, size_t index);
        MonitorIdentity FromTarget(DisplayTarget const& target, size_t index, MatchMethod method) const;

        std::vector<DisplayTarget> m_targets;
        std::vector<DisplayDeviceEntry> m_devices;
        std::set<std::wstring> m_claimed;
    };
}
