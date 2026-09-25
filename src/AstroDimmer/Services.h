#pragma once

#include <memory>
#include "AstroStage.h"
#include "Displays/DisplayMirror.h"
#include "Event.h"
#include "Settings.h"

namespace AstroDimmer
{
    /// What the windows share in the UI process: the settings, the displays
    /// and where the sun has us - each a copy of the host's, kept current
    /// over the link (see Link.h). Owned by the App, reached by the windows
    /// through Services::Get().
    struct Services
    {
        Core::AppSettings Settings;
        std::unique_ptr<Displays::DisplayMirror> Displays;

        /// Tracked even while the schedule is off: the flyout and Settings
        /// show it either way.
        AstroStage Stage{ AstroStage::Day };

        /// Raised when the stage changes, never on every tick.
        Event<AstroStage> StageChanged;

        /// Settings changed - here or in the host: the flyout may need to
        /// show or hide a row.
        Event<> SettingsChanged;

        /// Opening Settings is asked for from the flyout's gear and from the
        /// tray icon; the App owns the window.
        std::function<void()> OpenSettings;

        /// Quit, from the settings window: the host and the UI both.
        std::function<void()> Quit;

        /// Hands the settings to the host, which owns the file and applies
        /// them to the schedule.
        std::function<void()> SaveSettings;

        /// Persists the settings and tells everyone who cares.
        void SaveAndNotify()
        {
            if (SaveSettings) SaveSettings();
            SettingsChanged();
        }

        static Services& Get();
        static void Set(Services* services);
    };
}
