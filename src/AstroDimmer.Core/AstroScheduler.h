#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>
#include "AstroEngine.h"
#include "AstroSettings.h"

namespace AstroDimmer::Core
{
    /// A display the scheduler can drive.
    struct BrightnessTarget
    {
        std::wstring Key;

        /// Last known level, 0-100. Optimistic: set on write, not re-read.
        int Brightness{ 0 };

        bool Hidden{ false };

        /// This display's own levels. Each display carries its own pair
        /// because the same night-time level looks wrong across panels of
        /// different types - the whole point of setting them separately.
        int DayBrightness{ 100 };
        int NightBrightness{ 30 };

        /// The level just read back from the display itself, when this tick
        /// had one. Mid-fade it is checked against the step last written: a
        /// panel no longer showing our step was changed by something else.
        std::optional<int> ReadBack;
    };

    /// Why a tick did or did not write.
    enum class AstroOutcome
    {
        Applied,
        Disabled,
        NoSolarData,
        ManualOverride,
        Settled,
        Paused,

        /// A fade found a display no longer at the step it was last given,
        /// and stopped. The schedule then holds off until the next boundary.
        Interrupted,
    };

    /// One display and the level it should be set to.
    struct AstroWrite
    {
        std::wstring Key;
        int Brightness;

        bool operator==(AstroWrite const&) const = default;
    };

    struct AstroTick
    {
        AstroOutcome Outcome{ AstroOutcome::Disabled };

        /// The level for the schedule's DEFAULT day/night pair - what a
        /// display with no levels of its own would be set to. For
        /// diagnostics and anything that needs one representative number.
        int Brightness{ 0 };

        std::optional<AstroEngine::Period> Period;
        bool IsFading{ false };

        /// Displays actually written on this tick, with levels.
        std::vector<AstroWrite> Writes;
    };

    /// Drives day/night brightness, and stays out of the user's way.
    ///
    ///   - Manual override. If the user sets brightness themselves during a
    ///     period, their level stands until the next boundary.
    ///   - Echo rejection. The flyout re-sends whatever its sliders hold, and
    ///     those lag a fade that is still stepping. A level matching anything
    ///     this scheduler recently wrote is an echo, not a manual change -
    ///     otherwise a fade would cancel itself.
    ///   - Drift re-apply. If a display has wandered off the level we set, we
    ///     re-assert it.
    ///   - Fade step skipping. Mid-fade, a level already written on the
    ///     previous tick is not written again.
    ///   - Fade interruption. Mid-fade, a display read back at anything but
    ///     the step last written to it was changed by something else - the
    ///     monitor's own buttons, another app. The fade stops there and the
    ///     schedule yields until the next sunrise or sunset.
    ///
    /// Pure: the caller supplies the clock and the targets.
    class AstroScheduler
    {
    public:
        AstroSettings Settings;

        /// Suspends scheduling without losing state.
        bool Paused{ false };

        /// The period the user has taken control of, if any.
        std::optional<AstroEngine::Period> ManualOverridePeriod() const { return m_manualOverridePeriod; }

        /// Records that brightness was changed from outside this scheduler.
        ///
        /// fromUser is false for levels a window pushes on its own; those are
        /// not deliberate choices. Even when true, a level matching one this
        /// scheduler could have written is treated as an echo. deviceKey,
        /// where known, makes the echo test use that display's own levels.
        void NoteManualChange(std::optional<int> level, bool fromUser, DateTime const& localNow,
                              std::optional<std::wstring> const& deviceKey = std::nullopt);

        /// Evaluates the schedule. Returns which displays should be written
        /// and to what; the caller performs the writes.
        AstroTick Tick(DateTime const& localNow, std::vector<BrightnessTarget> const& targets, bool force = false);

        /// Clears remembered writes and any override. Call when the schedule's
        /// configuration changes, so new values apply immediately.
        void Reset();

    private:
        std::optional<AstroEngine::State> GetState(DateTime const& localNow) const;
        bool IsOurOwnLevel(int level, AstroEngine::State const& state,
                           std::optional<std::wstring> const& deviceKey) const;
        void RememberWrite(int level, DateTime const& at);

        struct RecentWrite
        {
            int Level;
            double AtMs;
        };

        /// Written levels remembered for echo rejection. A fade moves a step
        /// at a time and the UI can re-send a step or two behind, so the last
        /// value alone is not enough.
        std::vector<RecentWrite> m_recentWrites;

        std::optional<AstroEngine::Period> m_lastSeenPeriod;
        std::optional<AstroEngine::Period> m_manualOverridePeriod;
        std::optional<AstroEngine::Period> m_lastAppliedPeriod;

        /// Last level written per display, for fade-step skipping.
        std::map<std::wstring, int> m_lastWritten;

        /// Last level each display settled at, for the settled check.
        std::map<std::wstring, int> m_lastApplied;

        /// Levels written during the fade in progress, for the read-back
        /// check. Apart from m_lastWritten because only a step of THIS fade
        /// says what the panel should show: at the start of a fade that still
        /// holds the previous period's level, which a manual change during
        /// that period will have made stale.
        std::map<std::wstring, int> m_fadeWritten;
    };
}
