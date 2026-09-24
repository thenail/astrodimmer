#include "pch.h"
#include "MonitorMatcher.h"

namespace AstroDimmer::Core
{
    std::wstring DisplayTarget::DeviceKey() const { return MonitorMatcher::KeyOf(DevicePath); }

    std::wstring DisplayDeviceEntry::DeviceKey() const { return MonitorMatcher::KeyOf(DeviceId); }

    wchar_t const* ToString(MatchMethod method)
    {
        switch (method)
        {
        case MatchMethod::Description: return L"Description";
        case MatchMethod::TargetOrder: return L"TargetOrder";
        default: return L"DeviceOrder";
        }
    }

    MonitorMatcher::MonitorMatcher(std::vector<DisplayTarget> targets, std::vector<DisplayDeviceEntry> devices)
        : m_targets(std::move(targets)), m_devices(std::move(devices))
    {
    }

    std::wstring MonitorMatcher::KeyOf(std::wstring const& devicePath)
    {
        auto suffix = devicePath.find(L"#{");
        return suffix == std::wstring::npos ? devicePath : devicePath.substr(0, suffix);
    }

    std::optional<MonitorIdentity> MonitorMatcher::Match(std::wstring const& sourceName,
                                                         std::vector<std::wstring> const& descriptions, size_t index)
    {
        std::vector<DisplayTarget const*> onSource;
        for (auto const& t : m_targets)
            if (t.SourceName == sourceName)
                onSource.push_back(&t);

        auto identity = ByDescription(onSource, descriptions, index);
        if (!identity) identity = ByTargetOrder(onSource, index);
        if (!identity) identity = ByDeviceOrder(sourceName, index);

        if (identity)
            m_claimed.insert(identity->DeviceKey);

        return identity;
    }

    std::optional<MonitorIdentity> MonitorMatcher::ByDescription(std::vector<DisplayTarget const*> const& onSource,
                                                                 std::vector<std::wstring> const& descriptions,
                                                                 size_t index)
    {
        auto const& description = descriptions[index];
        if (description.empty())
            return std::nullopt;

        // Two identical panels on one HMONITOR share a description; it can't
        // tell them apart.
        if (std::count(descriptions.begin(), descriptions.end(), description) != 1)
            return std::nullopt;

        std::vector<DisplayTarget const*> named;
        for (auto const* t : onSource)
            if (t->FriendlyName == description)
                named.push_back(t);

        if (named.size() != 1 || m_claimed.contains(named[0]->DeviceKey()))
            return std::nullopt;

        return FromTarget(*named[0], index, MatchMethod::Description);
    }

    std::optional<MonitorIdentity> MonitorMatcher::ByTargetOrder(std::vector<DisplayTarget const*> const& onSource,
                                                                 size_t index)
    {
        if (index >= onSource.size() || m_claimed.contains(onSource[index]->DeviceKey()))
            return std::nullopt;

        return FromTarget(*onSource[index], index, MatchMethod::TargetOrder);
    }

    std::optional<MonitorIdentity> MonitorMatcher::ByDeviceOrder(std::wstring const& sourceName, size_t index)
    {
        size_t seen = 0;
        for (auto const& device : m_devices)
        {
            if (device.SourceName != sourceName)
                continue;

            if (seen++ != index)
                continue;

            if (m_claimed.contains(device.DeviceKey()))
                return std::nullopt;

            return MonitorIdentity{ device.DeviceKey(), device.DeviceId, device.DeviceName, MatchMethod::DeviceOrder };
        }

        return std::nullopt;
    }

    MonitorIdentity MonitorMatcher::FromTarget(DisplayTarget const& target, size_t index, MatchMethod method) const
    {
        // A target gives the key. The name and full path come from the
        // matching EnumDisplayDevices entry when there is one, since that is
        // the form the rest of Windows uses for the monitor.
        auto key = target.DeviceKey();
        for (auto const& device : m_devices)
            if (device.DeviceKey() == key)
                return { key, device.DeviceId, device.DeviceName, method };

        return { key, target.DevicePath, target.SourceName + L"\\Monitor" + std::to_wstring(index), method };
    }
}
