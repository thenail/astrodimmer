#include "pch.h"
#include "Native/DdcSession.h"
#include "Native/DisplayInfo.h"

namespace AstroDimmer::Native
{
    namespace
    {
        /// Codes nearly every DDC/CI monitor answers, tried in turn: 0x02 (new
        /// control value), 0xDF (VCP version), 0x10 (brightness). Any reply at
        /// all means the channel works.
        constexpr BYTE QuickProbeCodes[] = { 0x02, 0xDF, 0x10 };

        /// Pause before each successive attempt at a capability transaction.
        /// Many monitors ignore the first request and answer a repeat, so the
        /// first retry is immediate and later ones give the controller time.
        constexpr int LengthAttemptDelaysMs[] = { 0, 0, 100 };
        constexpr int ReplyAttemptDelaysMs[] = { 0, 0, 100, 100, 100 };

        constexpr int MaxAttempts = 3;
        constexpr int RetryDelayMs = 50;

        /// Errors that mean the message was mangled in transit rather than
        /// refused: the I2C transfer failed, or the reply came back with a bad
        /// checksum, length, command byte or timing status.
        bool IsBusGlitch(DWORD error)
        {
            switch (error)
            {
            case 0xC0262582: // ERROR_GRAPHICS_I2C_ERROR_TRANSMITTING_DATA
            case 0xC0262583: // ERROR_GRAPHICS_I2C_ERROR_RECEIVING_DATA
            case 0xC0262586: // ERROR_GRAPHICS_DDCCI_MONITOR_RETURNED_INVALID_TIMING_STATUS_BYTE
            case 0xC0262589: // ERROR_GRAPHICS_DDCCI_INVALID_MESSAGE_COMMAND
            case 0xC026258A: // ERROR_GRAPHICS_DDCCI_INVALID_MESSAGE_LENGTH
            case 0xC026258B: // ERROR_GRAPHICS_DDCCI_INVALID_MESSAGE_CHECKSUM
                return true;
            default:
                return false;
            }
        }

        template <size_t N, typename Call>
        bool Attempt(int const (&delaysMs)[N], Call&& call)
        {
            for (int delay : delaysMs)
            {
                if (delay > 0) Sleep(static_cast<DWORD>(delay));
                if (call()) return true;
            }
            return false;
        }

        std::wstring SystemMessage(DWORD code)
        {
            wchar_t* buffer = nullptr;
            DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                              FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
            std::wstring message = length ? std::wstring(buffer, length) : L"error " + std::to_wstring(code);
            LocalFree(buffer);

            while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r'))
                message.pop_back();
            return message;
        }

        void DestroyAll(std::vector<PHYSICAL_MONITOR> const& physicals)
        {
            for (auto const& p : physicals)
                DestroyPhysicalMonitor(p.hPhysicalMonitor);
        }
    }

    // ------------------------------------------------------------ refresh

    void DdcSession::Refresh(bool thorough, bool keepKnownGood, bool probeHighLevel)
    {
        ClearError();

        DWORD error = 0;
        auto opened = OpenAll(error);
        if (!opened)
        {
            // Nothing new to offer, so the previous set stays authoritative.
            // That beats dropping to zero displays on a transient failure.
            Fail(L"Could not open physical monitors", error);
            return;
        }

        auto next = Match(*opened, thorough, keepKnownGood, probeHighLevel);

        std::set<HANDLE> kept;
        for (auto const& [key, monitor] : next)
            kept.insert(monitor.Handle);

        for (auto const& o : *opened)
            for (auto const& p : o.Physicals)
                if (!kept.contains(p.hPhysicalMonitor))
                    DestroyPhysicalMonitor(p.hPhysicalMonitor);

        for (auto const& [key, old] : m_monitors)
            if (!kept.contains(old.Handle))
                DestroyPhysicalMonitor(old.Handle);

        m_monitors = std::move(next);
    }

    std::optional<std::vector<DdcSession::Opened>> DdcSession::OpenAll(DWORD& error)
    {
        // All or nothing: if any HMONITOR fails, whatever was opened is closed
        // and the refresh abandoned. A partial set would make monitors that
        // are still attached look unplugged.
        std::vector<Opened> opened;

        for (auto const& m : DisplayInfo::Monitors())
        {
            DWORD count = 0;
            if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m.Handle, &count))
            {
                error = GetLastError();
                for (auto const& o : opened) DestroyAll(o.Physicals);
                return std::nullopt;
            }

            std::vector<PHYSICAL_MONITOR> physicals(count);
            if (count > 0 && !GetPhysicalMonitorsFromHMONITOR(m.Handle, count, physicals.data()))
            {
                error = GetLastError();
                for (auto const& o : opened) DestroyAll(o.Physicals);
                return std::nullopt;
            }

            opened.push_back({ m.Handle, m.SourceName, std::move(physicals) });
        }

        return opened;
    }

    std::map<std::wstring, DdcSession::Attached> DdcSession::Match(std::vector<Opened> const& opened, bool thorough,
                                                                  bool keepKnownGood, bool probeHighLevel)
    {
        Core::MonitorMatcher matcher(DisplayInfo::Targets(), DisplayInfo::MonitorDevices());

        std::map<std::wstring, Attached> next;

        for (auto const& o : opened)
        {
            std::vector<std::wstring> descriptions;
            for (auto const& p : o.Physicals)
                descriptions.emplace_back(p.szPhysicalMonitorDescription);

            for (size_t i = 0; i < o.Physicals.size(); ++i)
            {
                auto identity = matcher.Match(o.SourceName, descriptions, i);
                if (!identity || next.contains(identity->DeviceKey))
                    continue; // left open here; destroyed by the caller

                Attached monitor;
                monitor.Handle = o.Physicals[i].hPhysicalMonitor;
                monitor.SourceName = o.SourceName;
                monitor.Description = descriptions[i];
                monitor.Identity = *identity;

                if (keepKnownGood)
                    Inherit(monitor);

                if (probeHighLevel && !monitor.SupportsHighLevelBrightness)
                {
                    DWORD min = 0, current = 0, max = 0;
                    monitor.SupportsHighLevelBrightness = GetMonitorBrightness(monitor.Handle, &min, &current, &max);
                }

                if (!monitor.SupportsVcp)
                    Validate(monitor, thorough);

                next.emplace(identity->DeviceKey, std::move(monitor));
            }
        }

        return next;
    }

    void DdcSession::Inherit(Attached& monitor)
    {
        // Carries a known-good verdict over from the previous refresh, so a
        // working monitor is not re-probed on every display change. The old
        // handle is kept when it still answers - some monitors take a moment
        // to talk on a freshly opened one.
        auto previous = m_monitors.find(monitor.Identity.DeviceKey);
        if (previous == m_monitors.end() || !previous->second.SupportsVcp ||
            previous->second.SourceName != monitor.SourceName ||
            previous->second.Identity.DevicePath != monitor.Identity.DevicePath)
            return;

        monitor.SupportsVcp = true;

        if (previous->second.Handle && AnswersQuickProbe(previous->second.Handle))
        {
            monitor.Handle = previous->second.Handle;
            monitor.SupportsHighLevelBrightness = previous->second.SupportsHighLevelBrightness;
        }
    }

    void DdcSession::Validate(Attached& monitor, bool thorough)
    {
        auto const& key = monitor.Identity.DeviceKey;

        // A capability string is proof enough; it came off this monitor.
        if (m_capabilities.contains(key))
        {
            monitor.SupportsVcp = true;
            return;
        }

        monitor.SupportsVcp = AnswersQuickProbe(monitor.Handle);

        if (thorough)
        {
            if (auto caps = ReadCapabilities(monitor.Handle); caps && !caps->empty())
            {
                m_capabilities[key] = *caps;
                monitor.SupportsVcp = true;
            }
        }
    }

    bool DdcSession::AnswersQuickProbe(HANDLE handle)
    {
        // Deliberately one attempt each, no retries - this runs for every
        // monitor on every refresh.
        for (BYTE code : QuickProbeCodes)
        {
            DWORD current = 0, max = 0;
            if (GetVCPFeatureAndVCPFeatureReply(handle, code, nullptr, &current, &max))
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------ listing

    std::vector<DdcMonitor> DdcSession::List() const
    {
        std::vector<DdcMonitor> list;
        for (auto const& [key, m] : m_monitors)
        {
            list.push_back({ key, m.SourceName, m.Identity.DeviceName, m.Identity.DevicePath, m.Description,
                             Core::ToString(m.Identity.Method), m.SupportsVcp, m.SupportsHighLevelBrightness });
        }

        // Listed in device-name order, which is how Windows numbers them.
        std::sort(list.begin(), list.end(),
                  [](auto const& a, auto const& b) { return a.DeviceName < b.DeviceName; });
        return list;
    }

    // ------------------------------------------------------------ VCP

    template <typename Call>
    bool DdcSession::WithRetry(Call&& call, wchar_t const* what)
    {
        // Repeats a transaction only when the failure is the kind a noisy I2C
        // bus produces. Anything else - an unsupported code, a monitor that
        // has gone - fails at once, since repeating it only adds latency.
        DWORD error = 0;
        for (int attempt = 0; attempt < MaxAttempts; ++attempt)
        {
            if (attempt > 0) Sleep(RetryDelayMs);
            if (call()) return true;

            error = GetLastError();
            if (!IsBusGlitch(error)) break;
        }

        Fail(what, error);
        return false;
    }

    std::optional<VcpReading> DdcSession::GetVcp(std::wstring const& deviceKey, BYTE code)
    {
        ClearError();
        auto handle = Find(deviceKey);
        if (!handle) return std::nullopt;

        DWORD current = 0, max = 0;
        if (!WithRetry([&] { return GetVCPFeatureAndVCPFeatureReply(*handle, code, nullptr, &current, &max); },
                       L"Could not read VCP code"))
            return std::nullopt;

        return VcpReading{ current, max };
    }

    bool DdcSession::SetVcp(std::wstring const& deviceKey, BYTE code, DWORD value)
    {
        ClearError();
        auto handle = Find(deviceKey);
        if (!handle) return false;

        return WithRetry([&] { return SetVCPFeature(*handle, code, value); }, L"Could not write VCP code");
    }

    std::optional<HighLevelReading> DdcSession::GetHighLevelBrightness(std::wstring const& deviceKey)
    {
        ClearError();
        auto handle = Find(deviceKey);
        if (!handle) return std::nullopt;

        DWORD min = 0, current = 0, max = 0;
        if (!WithRetry([&] { return GetMonitorBrightness(*handle, &min, &current, &max); },
                       L"Could not read high-level brightness"))
            return std::nullopt;

        return HighLevelReading{ current, min, max };
    }

    bool DdcSession::SetHighLevelBrightness(std::wstring const& deviceKey, DWORD value)
    {
        ClearError();
        auto handle = Find(deviceKey);
        if (!handle) return false;

        return WithRetry([&] { return SetMonitorBrightness(*handle, value); }, L"Could not write high-level brightness");
    }

    // ------------------------------------------------------------ capabilities

    std::optional<std::wstring> DdcSession::ReadCapabilities(HANDLE handle)
    {
        DWORD length = 0;
        if (!Attempt(LengthAttemptDelaysMs, [&] { return GetCapabilitiesStringLength(handle, &length); }) || length == 0)
            return std::nullopt;

        std::vector<char> buffer(length);
        if (!Attempt(ReplyAttemptDelaysMs,
                     [&] { return CapabilitiesRequestAndCapabilitiesReply(handle, buffer.data(), length); }))
            return std::nullopt;

        // ASCII per MCCS, and null-terminated.
        std::wstring caps;
        for (char c : buffer)
        {
            if (c == '\0') break;
            caps.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        return caps;
    }

    std::optional<std::wstring> DdcSession::GetCapabilities(std::wstring const& deviceKey)
    {
        ClearError();

        if (auto cached = m_capabilities.find(deviceKey); cached != m_capabilities.end())
            return cached->second;

        auto handle = Find(deviceKey);
        if (!handle) return std::nullopt;

        auto caps = ReadCapabilities(*handle);
        if (!caps || caps->empty())
        {
            Fail(L"Monitor did not return a capability string", 0);
            return std::wstring();
        }

        m_capabilities[deviceKey] = *caps;

        // It just talked DDC/CI to us, whatever the probe said.
        if (auto monitor = m_monitors.find(deviceKey); monitor != m_monitors.end())
            monitor->second.SupportsVcp = true;

        return caps;
    }

    // ------------------------------------------------------------ plumbing

    std::optional<HANDLE> DdcSession::Find(std::wstring const& deviceKey)
    {
        if (auto monitor = m_monitors.find(deviceKey); monitor != m_monitors.end())
            return monitor->second.Handle;

        Fail(L"Monitor not found", 0);
        return std::nullopt;
    }

    void DdcSession::ClearError()
    {
        m_lastErrorCode = 0;
        m_lastErrorMessage.clear();
    }

    void DdcSession::Fail(std::wstring const& what, DWORD code)
    {
        m_lastErrorCode = code;
        m_lastErrorMessage = code == 0 ? what : what + L": " + SystemMessage(code);
    }

    void DdcSession::Close()
    {
        for (auto const& [key, monitor] : m_monitors)
            DestroyPhysicalMonitor(monitor.Handle);
        m_monitors.clear();
    }
}
