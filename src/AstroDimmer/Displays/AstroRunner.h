#pragma once

#include "AstroScheduler.h"
#include "AstroStage.h"
#include "Displays/DisplayService.h"
#include "Settings.h"

namespace AstroDimmer::Displays
{
    /// Ticks the day/night schedule and applies its decisions to real
    /// displays. The scheduler itself is pure (Core); this supplies the clock,
    /// the targets and the writes, and stays thin so the logic stays testable.
    class AstroRunner
    {
    public:
        AstroRunner(DisplayService& displays, Core::AppSettings& settings,
                    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher);
        ~AstroRunner();

        Core::AstroScheduler Scheduler;

        /// Last tick's outcome, for diagnostics.
        Core::AstroTick LastTick;

        Event<Core::AstroTick const&> Ticked;

        /// Where the sun has us now. Tracked even while the schedule is off:
        /// the tray and the flyout show it either way.
        AstroStage Stage() const { return m_stage; }

        /// Raised when the stage changes, never on every tick.
        Event<AstroStage> StageChanged;

        void Start();

        /// Re-reads the configuration and applies it at once. Call after the
        /// schedule's settings change, so new values do not wait a minute.
        void SettingsChanged();

        /// Re-applies at once without clearing state. Used when the displays
        /// change: a monitor just plugged in is at whatever level it powered
        /// on with, and the scheduler would otherwise consider the period
        /// settled. A manual override is still respected.
        void ApplyNow() { Run(true); }

        /// A user-driven brightness change: the schedule yields for the rest
        /// of the period. Echo rejection lives in the scheduler.
        void NoteManualChange(int level, std::wstring const& deviceKey);

    private:
        void UpdateStage();
        void Run(bool force = false);

        /// Mid-fade: reads every display back before the next step, so a
        /// level changed behind our back stops the fade instead of being
        /// stepped over.
        winrt::fire_and_forget ReadBackThenApply();

        void Apply(bool force, std::map<std::wstring, int> const& readings = {});
        void ApplyContrast(Core::AstroTick const& tick);

        DisplayService& m_displays;
        Core::AppSettings& m_settings;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        AstroStage m_stage{ AstroStage::Day };

        /// A read-back is on the DDC thread; a tick arriving meanwhile is
        /// dropped rather than queued behind it.
        bool m_reading{ false };
    };
}
