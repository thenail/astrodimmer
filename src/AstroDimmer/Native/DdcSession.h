#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>
#include "MonitorMatcher.h"

namespace AstroDimmer::Native
{
    /// A monitor as the DDC/CI layer sees it.
    struct DdcMonitor
    {
        /// Stable identity, and the key every other call takes. Settings are
        /// keyed by it.
        std::wstring DeviceKey;

        /// The GDI source driving it, "\\.\DISPLAY2".
        std::wstring Name;

        /// Its DISPLAY_DEVICE name, "\\.\DISPLAY2\Monitor0".
        std::wstring DeviceName;
        std::wstring DevicePath;

        /// dxva2's description of the physical monitor.
        std::wstring Description;

        /// How the handle was tied to the device key. Diagnostic only.
        std::wstring MatchedBy;

        /// Answers raw VCP requests.
        bool DdcciSupported{ false };

        /// Answers the high-level brightness API, which some monitors do when
        /// raw VCP fails.
        bool HighLevelBrightnessSupported{ false };
    };

    struct VcpReading
    {
        DWORD Current;
        DWORD Max;
    };

    struct HighLevelReading
    {
        DWORD Current;
        DWORD Min;
        DWORD Max;
    };

    /// The set of physical-monitor handles currently open, and every hardware
    /// conversation held through them.
    ///
    /// Not thread-safe, by design: DdcChannel owns the only instance and only
    /// ever touches it from its own thread.
    ///
    /// Handle ownership is the thing to keep straight. A handle is open from
    /// GetPhysicalMonitorsFromHMONITOR until DestroyPhysicalMonitor, and every
    /// refresh opens a fresh set. After a refresh exactly the handles in
    /// m_monitors stay open; every other one - unmatched, duplicate, replaced,
    /// or from a monitor that has gone - is destroyed.
    class DdcSession
    {
    public:
        ~DdcSession() { Close(); }

        /// Re-enumerates monitors and decides which ones answer DDC/CI.
        ///
        /// thorough also asks for the capability string: slow, but finds
        /// monitors that ignore the quick probe. keepKnownGood skips
        /// re-validating a monitor that already worked, keeping its old handle
        /// if it still answers. probeHighLevel checks the high-level
        /// brightness API too.
        void Refresh(bool thorough, bool keepKnownGood, bool probeHighLevel);

        std::vector<DdcMonitor> List() const;

        std::optional<VcpReading> GetVcp(std::wstring const& deviceKey, BYTE code);
        bool SetVcp(std::wstring const& deviceKey, BYTE code, DWORD value);

        std::optional<HighLevelReading> GetHighLevelBrightness(std::wstring const& deviceKey);
        bool SetHighLevelBrightness(std::wstring const& deviceKey, DWORD value);

        /// The monitor's MCCS capability string, cached once it has answered.
        /// Empty when it does not answer; nothing when the key is unknown.
        std::optional<std::wstring> GetCapabilities(std::wstring const& deviceKey);

        DWORD LastErrorCode() const { return m_lastErrorCode; }
        std::wstring const& LastErrorMessage() const { return m_lastErrorMessage; }

        /// Closes every open handle.
        void Close();

    private:
        struct Attached
        {
            HANDLE Handle{};
            std::wstring SourceName;
            std::wstring Description;
            Core::MonitorIdentity Identity;
            bool SupportsVcp{ false };
            bool SupportsHighLevelBrightness{ false };
        };

        struct Opened
        {
            HMONITOR Monitor;
            std::wstring SourceName;
            std::vector<PHYSICAL_MONITOR> Physicals;
        };

        static std::optional<std::vector<Opened>> OpenAll(DWORD& error);
        std::map<std::wstring, Attached> Match(std::vector<Opened> const& opened, bool thorough,
                                               bool keepKnownGood, bool probeHighLevel);
        void Inherit(Attached& monitor);
        void Validate(Attached& monitor, bool thorough);
        static bool AnswersQuickProbe(HANDLE handle);
        static std::optional<std::wstring> ReadCapabilities(HANDLE handle);

        template <typename Call>
        bool WithRetry(Call&& call, wchar_t const* what);

        std::optional<HANDLE> Find(std::wstring const& deviceKey);
        void ClearError();
        void Fail(std::wstring const& what, DWORD code);

        /// Open monitors by device key.
        std::map<std::wstring, Attached> m_monitors;

        /// Capability strings by device key. Deliberately outlives refreshes:
        /// the read is slow and monitors answer it unreliably, so one that
        /// answered once is not asked again.
        std::map<std::wstring, std::wstring> m_capabilities;

        DWORD m_lastErrorCode{ 0 };
        std::wstring m_lastErrorMessage;
    };
}
