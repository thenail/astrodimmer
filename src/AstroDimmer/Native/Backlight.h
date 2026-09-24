#pragma once

#include <optional>
#include <string>
#include <vector>
#include <Wbemidl.h>

namespace AstroDimmer::Native
{
    /// A built-in panel whose backlight Windows lets us set.
    struct BacklightPanel
    {
        /// WMI's name for it, "DISPLAY\BOE0747\4&2d4ab9b&0&UID8388688_0",
        /// which every other call takes.
        std::wstring InstanceName;

        /// 0-100.
        int Current{ 0 };

        /// The percentages the panel actually takes, ascending. A request
        /// between two of them lands on one.
        std::vector<int> Levels;
    };

    /// The backlight of built-in panels, through WMI's WmiMonitorBrightness
    /// classes - the path Windows' own brightness slider and a laptop's
    /// brightness keys take. External monitors do not appear here; they
    /// answer DDC/CI instead.
    ///
    /// Lives on the DDC thread beside DdcSession, for the same reason: one
    /// place that talks to displays, one conversation at a time. That
    /// thread is a COM MTA, which WMI needs.
    class Backlight
    {
    public:
        /// Every panel WMI reports as active. Empty on machines without one,
        /// which is most desktops. Asked afresh each time rather than
        /// remembered as absent: a laptop's panel driver can come up after
        /// the app does.
        std::vector<BacklightPanel> List();

        std::optional<int> Get(std::wstring const& instanceName);
        bool Set(std::wstring const& instanceName, int percent);

        std::wstring const& LastErrorMessage() const { return m_lastErrorMessage; }

        /// Drops the WMI connection. The next call opens a fresh one.
        void Close();

    private:
        bool Connect();
        std::optional<std::vector<BacklightPanel>> Query();
        void Fail(std::wstring const& what, HRESULT hr);

        winrt::com_ptr<IWbemServices> m_services;

        /// WmiSetBrightness's parameter block, fetched once per connection.
        winrt::com_ptr<IWbemClassObject> m_setParams;

        std::wstring m_lastErrorMessage;
    };
}
