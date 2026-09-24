#include "pch.h"
#include "Displays/DisplayService.h"
#include "Backlight.h"
#include "DisplayDefaults.h"
#include "DisplayNames.h"
#include "Native/DisplayInfo.h"
#include "Strings.h"
#include "Trace.h"

using namespace std::chrono_literals;
using winrt::Microsoft::UI::Dispatching::DispatcherQueue;

namespace AstroDimmer::Displays
{
    namespace
    {
        constexpr BYTE VcpBrightness = 0x10;

        /// Contrast. No high-level API equivalent, so raw VCP only.
        constexpr BYTE VcpContrast = 0x12;

        /// How long after the last slider movement to wait before writing:
        /// short enough that dragging still feels live, long enough to
        /// collapse the bulk of the traffic.
        constexpr auto WriteDelay = 120ms;

        /// Grace after the display powers on before anything is written. The
        /// link is up before the panel is ready, and the first write into
        /// that window is the one that gets lost.
        constexpr auto WakeSettle = 2000ms;

        /// How long after a write before levels are synced back from the
        /// panels: some ramp to a new level rather than jump, and report the
        /// steps on the way.
        constexpr auto SyncSettle = 1500ms;

        /// Backoff between automatic retries. Bounded deliberately: each is
        /// real I2C traffic, and retrying forever would be hard on the
        /// hardware and hide a genuine "no DDC displays here".
        constexpr std::chrono::milliseconds RetryDelays[] = { 5000ms, 15000ms, 45000ms };

        constexpr wchar_t SimulatedFlag[] = L"--simulate-displays";

        /// How many simulated displays the command line asks for. The flag
        /// alone means one; a bad count is read as one rather than failing -
        /// this is a debug aid, not an API.
        int RequestedSimulatedCount()
        {
            int argc = 0;
            auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            int count = 0;

            for (int i = 1; i < argc; ++i)
            {
                std::wstring_view arg{ argv[i] };
                if (_wcsicmp(argv[i], SimulatedFlag) == 0)
                {
                    count = 1;
                }
                else if (arg.size() > std::size(SimulatedFlag) &&
                         _wcsnicmp(argv[i], SimulatedFlag, std::size(SimulatedFlag) - 1) == 0 &&
                         arg[std::size(SimulatedFlag) - 1] == L'=')
                {
                    int n = _wtoi(argv[i] + std::size(SimulatedFlag));
                    count = n > 0 ? std::min(n, 8) : 1;
                }
            }

            LocalFree(argv);
            return count;
        }

        /// Displays that exist only in the UI, for working on anything that
        /// needs more than one monitor on a machine that has one. Deliberately
        /// varied, since different names, sizes and levels are what shake out
        /// layout that only holds for identical rows.
        std::vector<std::shared_ptr<DisplayItem>> CreateSimulated(int count, size_t realCount)
        {
            struct Spec { wchar_t const* Name; wchar_t const* Vendor; wchar_t const* Mode; int Brightness; };
            static const Spec specs[] = {
                { L"DELL U2723QE", L"Dell", L"3840 × 2160", 45 },
                { L"LG HDR 4K", L"LG", L"3840 × 2160", 70 },
                { L"BenQ PD2700U", L"BenQ", L"3840 × 2160", 25 },
                { L"Generic PnP Monitor", L"Acer", L"1920 × 1080", 90 },
            };

            std::vector<std::shared_ptr<DisplayItem>> items;
            for (int i = 0; i < count; ++i)
            {
                auto number = std::to_wstring(realCount + i + 1);
                auto const& spec = specs[i % std::size(specs)];

                auto item = std::make_shared<DisplayItem>();
                item->DeviceKey = L"SIMULATED\\" + number;
                item->Name = spec.Name;
                item->Description = std::wstring(spec.Vendor) + L"  ·  Display " + number + L"  ·  " +
                                    spec.Mode + L"  ·  simulated";
                item->IsSimulated = true;

                // Real panels mostly answer VCP 0x12, and a fake one that did
                // not would leave the contrast UI untestable here.
                item->SupportsContrast = true;
                item->SetBrightness(spec.Brightness, BrightnessOrigin::Hardware);
                item->SetContrast(75, true);
                items.push_back(item);
            }
            return items;
        }

        int Percent(DWORD current, DWORD max)
        {
            return static_cast<int>(std::lround(current * 100.0 / max));
        }

        DWORD Raw(int percent, DWORD max)
        {
            return static_cast<DWORD>(std::clamp(static_cast<long>(std::lround(percent / 100.0 * max)), 0L,
                                                 static_cast<long>(max)));
        }
    }

    /// What one refresh learned about a display, gathered on the DDC thread
    /// and turned into a DisplayItem back on the UI thread.
    struct DisplayService::Probed
    {
        std::wstring DeviceKey;
        std::wstring Name;
        std::wstring Description;
        BrightnessPath Path{ BrightnessPath::Vcp };
        std::wstring BacklightInstance;
        std::vector<int> BacklightLevels;
        DWORD Max{ 100 };
        DWORD Current{ 0 };
        bool SupportsContrast{ false };
        DWORD MaxContrast{ 100 };
        DWORD Contrast{ 0 };
        std::optional<double> DiagonalInches;
    };

    DisplayService::DisplayService(DispatcherQueue const& dispatcher)
        : m_dispatcher(dispatcher), m_simulatedCount(RequestedSimulatedCount())
    {
        m_flushTimer = m_dispatcher.CreateTimer();
        m_flushTimer.IsRepeating(false);
        m_flushTimer.Tick([this](auto&&, auto&&) { FlushAsync(); });
    }

    DisplayService::~DisplayService()
    {
        m_flushTimer.Stop();
        CancelRetry();
    }

    std::shared_ptr<DisplayItem> DisplayService::Find(std::wstring const& deviceKey) const
    {
        for (auto const& item : m_displays)
            if (item->DeviceKey == deviceKey)
                return item;
        return nullptr;
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::RefreshAsync()
    {
        return RefreshCore(false, false);
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::RefreshCore(bool accurate, bool isRetry)
    {
        if (!m_displaysAwake)
        {
            // Deferred rather than dropped: the reason for the refresh (a
            // dock, a replug) is still true when the screen comes back.
            m_refreshDeferred = true;
            co_return;
        }

        auto dispatcher = m_dispatcher;
        std::vector<Probed> probed;

        // Looked up here, on the UI thread, for the name fallbacks below.
        auto displayFormat = Strings::Get(L"DisplayNumber");
        auto builtInName = Strings::Get(L"BuiltInDisplay");

        co_await m_ddc.Enter();
        {
            auto& session = m_ddc.Session();
            session.Refresh(accurate, !isRetry, true);

            for (auto const& m : session.List())
            {
                if (!m.DdcciSupported && !m.HighLevelBrightnessSupported)
                    continue;

                Probed p;
                p.DeviceKey = m.DeviceKey;

                // Raw VCP first; the high-level API is the fallback some
                // monitors answer when VCP 0x10 does not.
                if (auto vcp = session.GetVcp(m.DeviceKey, VcpBrightness))
                {
                    p.Current = vcp->Current;
                    p.Max = vcp->Max;
                }
                else if (auto hl = session.GetHighLevelBrightness(m.DeviceKey))
                {
                    p.Path = BrightnessPath::HighLevel;
                    p.Current = hl->Current;
                    p.Max = hl->Max;
                }
                else
                {
                    continue; // cannot read: not actionable
                }

                if (p.Max == 0) p.Max = 100;

                auto caps = session.GetCapabilities(m.DeviceKey);
                Trace::Log(L"caps for " + m.Name + L": " +
                           (!caps ? L"<null>" : caps->empty() ? L"<empty>" :
                            std::to_wstring(caps->size()) + L" chars: " + caps->substr(0, 80)));

                // Asked once, here, rather than when Settings opens: a panel
                // that does not answer 0x12 has no contrast control at all,
                // and the UI needs to know before it offers one.
                if (auto contrast = session.GetVcp(m.DeviceKey, VcpContrast))
                {
                    p.SupportsContrast = true;
                    p.MaxContrast = contrast->Max > 0 ? contrast->Max : 100;
                    p.Contrast = contrast->Current;
                }

                p.Name = Core::DisplayNames::Resolve(Native::DisplayInfo::FriendlyName(m.Name), caps.value_or(L""),
                                                     m.Name, m.DeviceKey, displayFormat);
                p.Description = Native::DisplayInfo::Describe(m.DeviceKey, m.Name);
                if (auto edid = Native::DisplayInfo::Edid(m.DeviceKey))
                    p.DiagonalInches = Core::EdidDiagonalInches(*edid);
                probed.push_back(std::move(p));
            }

            // Built-in panels, which have no DDC/CI and are driven through
            // the backlight Windows exposes instead. A panel DDC/CI already
            // answered for stays on that path.
            auto targets = Native::DisplayInfo::Targets();
            for (auto const& panel : m_ddc.Backlight().List())
            {
                auto instanceKey = Core::Backlight::KeyOf(panel.InstanceName);
                if (!instanceKey) continue;

                // Keyed as the DDC layer would key it, so settings carry over
                // whichever path reaches the panel. No target means it is not
                // on the desktop right now.
                auto target = std::find_if(targets.begin(), targets.end(), [&](Core::DisplayTarget const& t)
                {
                    return _wcsicmp(t.DeviceKey().c_str(), instanceKey->c_str()) == 0;
                });
                if (target == targets.end()) continue;

                auto key = target->DeviceKey();
                if (std::any_of(probed.begin(), probed.end(), [&](Probed const& q) { return q.DeviceKey == key; }))
                    continue;

                Probed p;
                p.DeviceKey = key;
                p.Path = BrightnessPath::Backlight;
                p.BacklightInstance = panel.InstanceName;
                p.BacklightLevels = panel.Levels;
                p.Current = static_cast<DWORD>(panel.Current);

                // A built-in panel rarely has a name of its own, and "BOE
                // Display 1" names whoever made the glass, not the laptop.
                auto friendly = Native::DisplayInfo::FriendlyName(target->SourceName);
                p.Name = friendly ? *friendly : builtInName;
                p.Description = Native::DisplayInfo::Describe(key, target->SourceName);
                if (auto edid = Native::DisplayInfo::Edid(key))
                    p.DiagonalInches = Core::EdidDiagonalInches(*edid);

                Trace::Log(L"backlight: " + panel.InstanceName + L" -> " + key + L" at " +
                           std::to_wstring(panel.Current) + L"% (" + std::to_wstring(panel.Levels.size()) + L" levels)");
                probed.push_back(std::move(p));
            }
        }
        co_await wil::resume_foreground(dispatcher);

        std::vector<std::shared_ptr<DisplayItem>> items;
        for (auto const& p : probed)
        {
            auto item = std::make_shared<DisplayItem>();
            item->DeviceKey = p.DeviceKey;
            item->Name = p.Name;
            item->Description = p.Description;
            item->Path = p.Path;
            item->BacklightInstance = p.BacklightInstance;
            item->BacklightLevels = p.BacklightLevels;
            item->MaxBrightness = p.Max;
            item->SupportsContrast = p.SupportsContrast;
            item->MaxContrast = p.MaxContrast;
            item->DiagonalInches = p.DiagonalInches;
            item->SetBrightness(Percent(p.Current, p.Max), BrightnessOrigin::Hardware);
            if (p.SupportsContrast)
                item->SetContrast(Percent(p.Contrast, p.MaxContrast), true);
            items.push_back(item);
        }

        size_t realCount = items.size();
        size_t builtInCount = std::count_if(probed.begin(), probed.end(),
                                            [](Probed const& p) { return p.Path == BrightnessPath::Backlight; });

        // Wired like any other row, so a drag on a fake display still suspends
        // the schedule and exercises the coalescing path; only the hardware
        // write at the end is skipped.
        for (auto& sim : CreateSimulated(m_simulatedCount, realCount))
            items.push_back(sim);

        for (auto const& item : items)
            Attach(item);

        m_displays = std::move(items);
        DisplaysChanged();

        StatusChanged(m_displays.empty() ? Strings::Get(L"NoAdjustableDisplays") : std::wstring());

        // The real count, not the displayed one: simulated rows must not make
        // a failed probe look successful and cancel the retry ladder. Nor
        // must a laptop's own panel, which says nothing about DDC/CI.
        EvaluateRetry(realCount - builtInCount, builtInCount);
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::ReadBrightnessAsync(std::map<std::wstring, int>& readings)
    {
        return ReadLevelsAsync(&readings, false);
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::SyncLevelsAsync()
    {
        // A panel asked straight after a write can still be on its way to the
        // new level, and would drag the slider back to where it came from.
        if (m_syncing || std::chrono::steady_clock::now() - m_lastWrite < SyncSettle) co_return;

        m_syncing = true;
        co_await ReadLevelsAsync(nullptr, true);
        m_syncing = false;
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::ReadLevelsAsync(std::map<std::wstring, int>* readings, bool sync)
    {
        // A sleeping panel does not answer its bus, and one with a write
        // still waiting to go out would report the level before it.
        if (!m_displaysAwake) co_return;

        struct Read
        {
            std::wstring Key;
            BrightnessPath Path;
            std::wstring BacklightInstance;
            bool Brightness;
            bool Contrast;
            std::optional<DWORD> Raw;
            std::optional<DWORD> RawContrast;
        };

        std::vector<Read> reads;
        for (auto const& item : m_displays)
        {
            if (item->IsSimulated) continue;

            bool brightness = !m_pending.contains(item->DeviceKey);
            bool contrast = sync && item->SupportsContrast && !m_pendingContrast.contains(item->DeviceKey);
            if (brightness || contrast)
                reads.push_back({ item->DeviceKey, item->Path, item->BacklightInstance, brightness, contrast });
        }

        if (reads.empty()) co_return;

        auto dispatcher = m_dispatcher;
        auto generation = m_writeGeneration;
        co_await m_ddc.Enter();
        {
            auto& session = m_ddc.Session();
            for (auto& r : reads)
            {
                if (r.Brightness)
                {
                    if (r.Path == BrightnessPath::Backlight)
                    {
                        if (auto percent = m_ddc.Backlight().Get(r.BacklightInstance))
                            r.Raw = static_cast<DWORD>(*percent);
                    }
                    else if (r.Path == BrightnessPath::HighLevel)
                    {
                        if (auto hl = session.GetHighLevelBrightness(r.Key)) r.Raw = hl->Current;
                    }
                    else if (auto vcp = session.GetVcp(r.Key, VcpBrightness))
                    {
                        r.Raw = vcp->Current;
                    }
                }

                if (r.Contrast)
                    if (auto vcp = session.GetVcp(r.Key, VcpContrast))
                        r.RawContrast = vcp->Current;
            }
        }
        co_await wil::resume_foreground(dispatcher);

        // Writes went out while we were reading: what came back may be the
        // level they replaced, and the next read will have the truth.
        if (m_writeGeneration != generation) co_return;

        for (auto const& r : reads)
        {
            // Gone during the read, or a write queued meanwhile: either way
            // the value is no longer the one that matters.
            auto item = Find(r.Key);
            if (!item) continue;

            if (r.Raw && !m_pending.contains(r.Key))
            {
                // Compared in the panel's own units: on a display whose range
                // is not 0-100, a percentage does not survive the round trip,
                // and the level we wrote would read back as its neighbour. A
                // stepped backlight is the same story: it settles on the level
                // nearest to the one it was asked for.
                DWORD expected = item->Path == BrightnessPath::Backlight
                                     ? static_cast<DWORD>(Core::Backlight::Snap(item->Brightness(), item->BacklightLevels))
                                     : Raw(item->Brightness(), item->MaxBrightness);
                bool changed = *r.Raw != expected;
                int percent = changed ? Percent(*r.Raw, item->MaxBrightness) : item->Brightness();

                if (changed && sync)
                    Trace::Log(L"sync: " + r.Key + L" brightness " + std::to_wstring(item->Brightness()) + L"% -> " +
                               std::to_wstring(percent) + L"%");

                item->SetBrightness(percent, BrightnessOrigin::Hardware);
                if (readings) (*readings)[r.Key] = percent;
                if (changed && sync) ExternalChangedBrightness(*item);
            }

            if (r.RawContrast && !m_pendingContrast.contains(r.Key) &&
                *r.RawContrast != Raw(item->Contrast(), item->MaxContrast))
            {
                int percent = Percent(*r.RawContrast, item->MaxContrast);
                Trace::Log(L"sync: " + r.Key + L" contrast " + std::to_wstring(item->Contrast()) + L"% -> " +
                           std::to_wstring(percent) + L"%");
                item->SetContrast(percent, true);
            }
        }
    }

    void DisplayService::Attach(std::shared_ptr<DisplayItem> const& item)
    {
        item->BrightnessChanged.Add([this](DisplayItem& display, BrightnessOrigin origin)
        {
            if (origin == BrightnessOrigin::User)
                UserChangedBrightness(display);

            m_pending[display.DeviceKey] = display.Brightness();

            // Queued but not scheduled while the screen is off -
            // SetDisplayPowerAsync starts the clock again on wake.
            if (m_displaysAwake)
                ScheduleFlush(WriteDelay);
        });

        item->ContrastChanged.Add([this](DisplayItem& display)
        {
            // Same coalescing and the same timer as brightness, so a schedule
            // that moves both sends one of each rather than interleaving them.
            m_pendingContrast[display.DeviceKey] = display.Contrast();
            if (m_displaysAwake)
                ScheduleFlush(WriteDelay);
        });

    }

    void DisplayService::ScheduleFlush(std::chrono::milliseconds delay)
    {
        // Restarting the timer means the write lands once movement stops,
        // rather than once per tick during a drag.
        m_flushTimer.Stop();
        m_flushTimer.Interval(delay);
        m_flushTimer.Start();
    }

    winrt::fire_and_forget DisplayService::FlushAsync()
    {
        m_flushTimer.Stop();

        // The single gate every write passes through, whether it came from a
        // slider, the schedule or a wake. The queue stays intact, so nothing
        // is lost if the display sleeps mid-drag.
        if (!m_displaysAwake) co_return;
        if (m_pending.empty() && m_pendingContrast.empty()) co_return;

        struct Write
        {
            std::wstring Key;
            DWORD Raw;
            BrightnessPath Path;
            bool Contrast;
            std::wstring BacklightInstance;
        };

        std::vector<Write> writes;

        for (auto const& [key, percent] : m_pending)
        {
            auto item = Find(key);

            // Nothing on the far end of a simulated display.
            if (!item || item->IsSimulated) continue;

            writes.push_back({ key, Raw(percent, item->MaxBrightness), item->Path, false, item->BacklightInstance });
        }

        for (auto const& [key, percent] : m_pendingContrast)
        {
            auto item = Find(key);

            // Belt and braces: a write to an unsupported code is I2C traffic
            // that can only fail.
            if (!item || item->IsSimulated || !item->SupportsContrast) continue;

            writes.push_back({ key, Raw(percent, item->MaxContrast), BrightnessPath::Vcp, true });
        }

        m_pending.clear();
        m_pendingContrast.clear();

        if (writes.empty()) co_return;

        ++m_writeGeneration;
        m_lastWrite = std::chrono::steady_clock::now();

        co_await m_ddc.Enter();

        auto& session = m_ddc.Session();
        auto& backlight = m_ddc.Backlight();
        for (auto const& w : writes)
        {
            if (w.Path == BrightnessPath::Backlight)
            {
                if (!backlight.Set(w.BacklightInstance, static_cast<int>(w.Raw)))
                    Trace::Log(L"backlight: write failed for " + w.Key + L": " + backlight.LastErrorMessage());
                continue;
            }

            bool ok = w.Contrast ? session.SetVcp(w.Key, VcpContrast, w.Raw)
                    : w.Path == BrightnessPath::HighLevel ? session.SetHighLevelBrightness(w.Key, w.Raw)
                    : session.SetVcp(w.Key, VcpBrightness, w.Raw);

            if (!ok)
                Trace::Log(L"ddc: write failed for " + w.Key + L": " + session.LastErrorMessage());
        }
    }

    winrt::Windows::Foundation::IAsyncAction DisplayService::SetDisplayPowerAsync(bool on)
    {
        if (m_displaysAwake == on) co_return;
        m_displaysAwake = on;

        if (!on)
        {
            // Stop the clock, keep the queue. Whatever the schedule decides
            // while the screen is off is still right when it comes back.
            m_flushTimer.Stop();
            co_return;
        }

        // Re-enumerate first if a refresh was held back, since the writes
        // below need handles that resolve.
        if (m_refreshDeferred)
        {
            m_refreshDeferred = false;
            co_await RefreshAsync();
        }

        if (!m_pending.empty() || !m_pendingContrast.empty())
            ScheduleFlush(WakeSettle);
    }

    void DisplayService::EvaluateRetry(size_t found, size_t builtIn)
    {
        CancelRetry();

        if (found > 0)
        {
            m_retryAttempt = 0;
            return;
        }

        // Only retry when Windows reports monitors beyond the built-in panels
        // but DDC/CI found none: that mismatch is the signature of a failed
        // probe, rather than of panels that genuinely lack DDC/CI (a laptop
        // on its own, many TVs).
        if (Native::DisplayInfo::SystemMonitorCount() <= static_cast<int>(builtIn))
        {
            m_retryAttempt = 0;
            return;
        }

        if (m_retryAttempt >= std::size(RetryDelays))
        {
            RetryStatus(Strings::Get(L"RetryGaveUp"));
            return;
        }

        auto delay = RetryDelays[m_retryAttempt];
        ++m_retryAttempt;

        RetryStatus(Strings::Format(L"RetryScheduled", { std::to_wstring(delay.count() / 1000),
                                                         std::to_wstring(m_retryAttempt),
                                                         std::to_wstring(std::size(RetryDelays)) }));

        m_retryTimer = m_dispatcher.CreateTimer();
        m_retryTimer.IsRepeating(false);
        m_retryTimer.Interval(delay);
        m_retryTimer.Tick([this](auto&&, auto&&)
        {
            CancelRetry();

            // Escalate after the first attempt: the accurate probe also asks
            // for the capability string, which finds monitors that ignored
            // the quick one.
            RefreshCore(m_retryAttempt >= 2, true);
        });
        m_retryTimer.Start();
    }

    void DisplayService::CancelRetry()
    {
        if (m_retryTimer)
        {
            m_retryTimer.Stop();
            m_retryTimer = nullptr;
        }
    }

    void DisplayService::ResetRetries()
    {
        CancelRetry();
        m_retryAttempt = 0;
    }
}
