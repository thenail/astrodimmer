#include "pch.h"
#include "AstroScheduler.h"

namespace AstroDimmer::Core
{
    using AstroEngine::Period;

    namespace
    {
        /// How long a written level is remembered for echo rejection.
        constexpr double WriteMemoryMs = 15 * MsPerMinute;
    }

    void AstroScheduler::NoteManualChange(std::optional<int> level, bool fromUser, DateTime const& localNow,
                                          std::optional<std::wstring> const& deviceKey)
    {
        if (!fromUser || !Settings.Enabled) return;
        if (m_manualOverridePeriod) return;

        auto state = GetState(localNow);
        if (!state) return;

        if (level && IsOurOwnLevel(*level, *state, deviceKey))
            return;

        m_manualOverridePeriod = state->Current;
    }

    bool AstroScheduler::IsOurOwnLevel(int level, AstroEngine::State const& state,
                                       std::optional<std::wstring> const& deviceKey) const
    {
        if (deviceKey)
        {
            if (auto last = m_lastWritten.find(*deviceKey); last != m_lastWritten.end() && last->second == level)
                return true;
        }
        else
        {
            // No key: any display we have written this level to recently counts.
            for (auto const& [key, written] : m_lastWritten)
                if (written == level)
                    return true;
        }

        DisplayLevels levels;
        if (deviceKey)
        {
            levels = Settings.LevelsFor(*deviceKey);
        }
        else
        {
            levels.Day = Settings.DayBrightness;
            levels.Night = Settings.NightBrightness;
        }

        if (AstroEngine::GetBrightness(Period::Day, levels.Day, levels.Night) == level) return true;
        if (AstroEngine::GetBrightness(Period::Night, levels.Day, levels.Night) == level) return true;
        if (AstroEngine::GetTargetBrightness(state, levels.Day, levels.Night) == level) return true;

        return std::any_of(m_recentWrites.begin(), m_recentWrites.end(),
                           [level](RecentWrite const& w) { return w.Level == level; });
    }

    std::optional<AstroEngine::State> AstroScheduler::GetState(DateTime const& localNow) const
    {
        return AstroEngine::GetState(Settings.Latitude, Settings.Longitude, Settings.DayOffset,
                                     Settings.NightOffset, Settings.EffectiveFadeMinutes(), localNow);
    }

    AstroTick AstroScheduler::Tick(DateTime const& localNow, std::vector<BrightnessTarget> const& targets, bool force)
    {
        if (!Settings.Enabled)
            return { AstroOutcome::Disabled };

        if (Paused)
            return { AstroOutcome::Paused };

        auto maybeState = GetState(localNow);
        if (!maybeState)
            return { AstroOutcome::NoSolarData };

        auto const& state = *maybeState;

        // Crossing into a new period hands control back to the schedule.
        if (m_lastSeenPeriod != state.Current)
        {
            m_lastSeenPeriod = state.Current;
            m_manualOverridePeriod.reset();
            m_fadeWritten.clear();
        }

        // The user picked their own level this period; stay out of the way,
        // even on a forced run.
        if (m_manualOverridePeriod == state.Current)
            return { AstroOutcome::ManualOverride, 0, state.Current };

        bool isFading = state.Progress < 1;

        // Something other than us moved a display mid-fade. Stop at once
        // rather than walk it back: whoever changed it meant it. Not on a
        // forced run - a display that has just appeared reads whatever it
        // powered on with, and that is no one's choice.
        if (isFading && !force)
        {
            bool changed = std::any_of(targets.begin(), targets.end(), [&](BrightnessTarget const& t)
            {
                if (t.Hidden || !t.ReadBack) return false;
                auto written = m_fadeWritten.find(t.Key);
                return written != m_fadeWritten.end() && written->second != *t.ReadBack;
            });

            if (changed)
            {
                m_manualOverridePeriod = state.Current;
                m_fadeWritten.clear();
                return { AstroOutcome::Interrupted, 0, state.Current, true };
            }
        }

        // The nominal level, for the tick's summary figure. Each display's own
        // target is worked out below from its own pair.
        int brightness = AstroEngine::GetTargetBrightness(state, Settings.DayBrightness, Settings.NightBrightness);

        std::vector<BrightnessTarget const*> visible;
        for (auto const& t : targets)
            if (!t.Hidden)
                visible.push_back(&t);

        std::map<std::wstring, int> wanted;
        for (auto const* t : visible)
            wanted[t->Key] = AstroEngine::GetTargetBrightness(state, t->DayBrightness, t->NightBrightness);

        bool drifted = std::any_of(visible.begin(), visible.end(),
                                   [&](auto const* t) { return t->Brightness != wanted[t->Key]; });

        bool settled = !isFading
                       && !drifted
                       && m_lastAppliedPeriod == state.Current
                       && std::all_of(visible.begin(), visible.end(), [&](auto const* t)
                          {
                              auto applied = m_lastApplied.find(t->Key);
                              return applied != m_lastApplied.end() && applied->second == wanted[t->Key];
                          });

        if (!force && settled)
            return { AstroOutcome::Settled, brightness, state.Current };

        // Mid-fade, do not repeat a level already sent to THAT display on the
        // previous tick. Deliberately not based on each display's reported
        // brightness: that is set optimistically on write and may never be
        // re-read, so it cannot say what the panel really shows.
        std::vector<AstroWrite> writes;
        for (auto const* t : visible)
        {
            auto last = m_lastWritten.find(t->Key);
            bool alreadySent = isFading && last != m_lastWritten.end() && last->second == wanted[t->Key];
            if (!alreadySent)
                writes.push_back({ t->Key, wanted[t->Key] });
        }

        for (auto const* t : visible)
        {
            m_lastWritten[t->Key] = wanted[t->Key];
            RememberWrite(wanted[t->Key], localNow);
        }

        if (isFading)
            for (auto const* t : visible)
                m_fadeWritten[t->Key] = wanted[t->Key];
        else
            m_fadeWritten.clear();

        if (isFading)
        {
            // More steps to come, so the period is not done.
            m_lastAppliedPeriod.reset();
            m_lastApplied.clear();
        }
        else
        {
            m_lastAppliedPeriod = state.Current;
            for (auto const* t : visible)
                m_lastApplied[t->Key] = wanted[t->Key];
        }

        return { AstroOutcome::Applied, brightness, state.Current, isFading, std::move(writes) };
    }

    void AstroScheduler::RememberWrite(int level, DateTime const& at)
    {
        double atMs = NaiveMs(at);
        std::erase_if(m_recentWrites, [atMs](RecentWrite const& w) { return atMs - w.AtMs >= WriteMemoryMs; });
        m_recentWrites.push_back({ level, atMs });
    }

    void AstroScheduler::Reset()
    {
        m_recentWrites.clear();
        m_manualOverridePeriod.reset();
        m_lastAppliedPeriod.reset();
        m_lastApplied.clear();
        m_lastWritten.clear();
        m_fadeWritten.clear();
    }
}
