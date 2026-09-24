#include "pch.h"
#include "Displays/AstroRunner.h"
#include "Trace.h"

using namespace std::chrono_literals;

namespace AstroDimmer::Displays
{
    namespace
    {
        /// A minute is fine-grained enough for a schedule measured in sunrise
        /// offsets.
        constexpr auto TickInterval = 60s;

        wchar_t const* ToString(Core::AstroOutcome outcome)
        {
            switch (outcome)
            {
            case Core::AstroOutcome::Applied: return L"Applied";
            case Core::AstroOutcome::Disabled: return L"Disabled";
            case Core::AstroOutcome::NoSolarData: return L"NoSolarData";
            case Core::AstroOutcome::ManualOverride: return L"ManualOverride";
            case Core::AstroOutcome::Settled: return L"Settled";
            case Core::AstroOutcome::Interrupted: return L"Interrupted";
            default: return L"Paused";
            }
        }
    }

    AstroRunner::AstroRunner(DisplayService& displays, Core::AppSettings& settings,
                             winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
        : m_displays(displays), m_settings(settings)
    {
        Scheduler.Settings = settings.Astro;

        m_timer = dispatcher.CreateTimer();
        m_timer.Interval(TickInterval);
        m_timer.IsRepeating(true);
        m_timer.Tick([this](auto&&, auto&&) { Run(); });

        m_stage = StageFor(m_settings.Astro, Core::LocalNow());

        Ticked.Add([](Core::AstroTick const& t)
        {
            std::wstring writes;
            for (auto const& w : t.Writes)
                writes += (writes.empty() ? L"" : L", ") + w.Key + L"@" + std::to_wstring(w.Brightness) + L"%";

            std::wstring period = !t.Period ? L"" : *t.Period == Core::AstroEngine::Period::Day ? L"Day" : L"Night";
            Trace::Log(std::wstring(L"astro: ") + ToString(t.Outcome) + L" period=" + period + L" nominal=" +
                       std::to_wstring(t.Brightness) + L" fading=" + (t.IsFading ? L"True" : L"False") +
                       L" wrote=[" + writes + L"]");
        });
    }

    AstroRunner::~AstroRunner()
    {
        m_timer.Stop();
    }

    void AstroRunner::Start()
    {
        m_timer.Start();

        // Apply at once rather than waiting a full interval, so brightness is
        // right at login rather than up to a minute later.
        Run(true);
    }

    void AstroRunner::SettingsChanged()
    {
        Scheduler.Settings = m_settings.Astro;
        Scheduler.Reset();
        Run(true);
    }

    void AstroRunner::NoteManualChange(int level, std::wstring const& deviceKey)
    {
        Scheduler.NoteManualChange(level, true, Core::LocalNow(), deviceKey);
        UpdateStage();
    }

    void AstroRunner::UpdateStage()
    {
        // Ahead of the schedule's own early exits: it has to keep working
        // with no displays, or with scheduling switched off entirely.
        auto now = Core::LocalNow();
        auto stage = StageFor(m_settings.Astro, now);

        // A fade the user took over - or one interrupted by a change from
        // outside - is no longer being walked, so the icon stops saying it is.
        if (stage == AstroStage::Transition)
        {
            auto const& a = m_settings.Astro;
            auto overridden = Scheduler.ManualOverridePeriod();
            if (overridden && overridden == Core::AstroEngine::GetPeriod(a.Latitude, a.Longitude, a.DayOffset,
                                                                         a.NightOffset, now))
                stage = *overridden == Core::AstroEngine::Period::Day ? AstroStage::Day : AstroStage::Night;
        }

        if (stage == m_stage) return;

        m_stage = stage;
        StageChanged(stage);
    }

    void AstroRunner::Run(bool force)
    {
        UpdateStage();

        // Only a fade has a step to check against; outside one, drift
        // re-apply already covers a display that wandered. A forced run skips
        // it too: it exists to set levels at once, and the scheduler would
        // not act on the readings anyway.
        bool readBack = !force && m_stage == AstroStage::Transition && Scheduler.Settings.Enabled &&
                        !Scheduler.Paused && !Scheduler.ManualOverridePeriod();

        if (readBack)
        {
            if (!m_reading) ReadBackThenApply();
            return;
        }

        Apply(force);
    }

    winrt::fire_and_forget AstroRunner::ReadBackThenApply()
    {
        m_reading = true;

        std::map<std::wstring, int> readings;
        co_await m_displays.ReadBrightnessAsync(readings);

        m_reading = false;
        Apply(false, readings);
    }

    void AstroRunner::Apply(bool force, std::map<std::wstring, int> const& readings)
    {
        std::vector<Core::BrightnessTarget> targets;
        for (auto const& d : m_displays.Displays())
        {
            auto levels = m_settings.Astro.LevelsFor(d->DeviceKey);

            // Hidden from the flyout means left alone entirely: the switch on
            // the display's card is the one place that decides whether
            // AstroDimmer manages a monitor.
            Core::BrightnessTarget target{ d->DeviceKey, d->Brightness(),
                                           m_settings.HiddenDisplays.contains(d->DeviceKey), levels.Day, levels.Night };

            if (auto read = readings.find(d->DeviceKey); read != readings.end())
                target.ReadBack = read->second;

            targets.push_back(std::move(target));
        }

        if (targets.empty())
            return;

        auto tick = Scheduler.Tick(Core::LocalNow(), targets, force);
        LastTick = tick;

        for (auto const& write : tick.Writes)
        {
            // The same path a slider drag takes, so the write is coalesced.
            if (auto item = m_displays.Find(write.Key))
                item->SetBrightness(write.Brightness, BrightnessOrigin::Schedule);
        }

        ApplyContrast(tick);

        // An interrupted fade has just ended the transition.
        if (tick.Outcome == Core::AstroOutcome::Interrupted)
            UpdateStage();

        Ticked(tick);
    }

    void AstroRunner::ApplyContrast(Core::AstroTick const& tick)
    {
        // Contrast follows the same solar curve between that display's own two
        // endpoints, on the ticks where brightness was written. Not routed
        // through the scheduler: its override and echo rules exist because
        // there is a brightness slider to fight with.
        if (tick.Outcome != Core::AstroOutcome::Applied) return;

        auto const& a = m_settings.Astro;
        auto state = Core::AstroEngine::GetState(a.Latitude, a.Longitude, a.DayOffset, a.NightOffset,
                                                 a.EffectiveFadeMinutes(), Core::LocalNow());
        if (!state) return;

        std::wstring applied;
        for (auto const& item : m_displays.Displays())
        {
            if (!item->SupportsContrast) continue;
            if (m_settings.HiddenDisplays.contains(item->DeviceKey)) continue;

            auto levels = a.LevelsFor(item->DeviceKey);
            if (!levels.Contrast) continue;

            int target = Core::AstroEngine::GetTargetBrightness(*state, levels.DayContrast, levels.NightContrast);
            item->SetContrast(target);
            applied += (applied.empty() ? L"" : L", ") + item->DeviceKey + L"@" + std::to_wstring(target) + L"%";
        }

        // Its own line: a silent tick here means "nobody asked", which is
        // worth telling apart from "it ran and did nothing".
        if (!applied.empty())
            Trace::Log(L"contrast: wrote=[" + applied + L"]");
    }
}
