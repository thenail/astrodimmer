#pragma once

#include <memory>
#include "Displays/AstroRunner.h"
#include "Displays/DisplayService.h"
#include "Event.h"
#include "Native/SystemEvents.h"
#include "Settings.h"

namespace AstroDimmer
{
    /// Everything that outlives any one window: the settings, the displays,
    /// the schedule and the system notifications. Owned by the App, reached
    /// by the windows through Services::Get().
    struct Services
    {
        Core::AppSettings Settings;
        std::unique_ptr<Displays::DisplayService> Displays;
        std::unique_ptr<Displays::AstroRunner> Astro;
        std::unique_ptr<Native::SystemEvents> Events;

        /// Settings changed in the settings window: the flyout may need to
        /// show or hide a row, and the schedule to re-apply at once.
        Event<> SettingsChanged;

        /// Opening Settings is asked for from the flyout's gear and from the
        /// tray icon; the App owns the window.
        std::function<void()> OpenSettings;

        /// Quit, from the settings window.
        std::function<void()> Quit;

        /// Persists the settings and tells everyone who cares.
        void SaveAndNotify()
        {
            Settings.Save();
            SettingsChanged();
        }

        static Services& Get();
        static void Set(Services* services);
    };
}
