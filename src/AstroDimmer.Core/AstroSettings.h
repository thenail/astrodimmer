#pragma once

#include <algorithm>
#include <map>
#include <string>

namespace AstroDimmer::Core
{
    /// One display's day and night levels, 0-100.
    struct DisplayLevels
    {
        int Day{ 100 };
        int Night{ 30 };

        /// Whether the schedule drives this display's CONTRAST as well as its
        /// brightness. Off by default, and deliberately so: contrast is the
        /// setting people calibrate a panel with, and moving it uninvited
        /// would undo that work on every monitor.
        bool Contrast{ false };

        /// Daytime contrast. A newly seen display starts with both at what it
        /// reads (see RecommendLevels), so switching the feature on does not
        /// immediately move anything; 75/50 are for older settings files.
        int DayContrast{ 75 };

        int NightContrast{ 50 };
    };

    /// Day/night scheduling configuration.
    struct AstroSettings
    {
        bool Enabled{ false };

        /// 0,0 means "not configured" (see AstroEngine).
        double Latitude{ 0 };
        double Longitude{ 0 };

        /// Minutes to shift each boundary; clamped to +/-180.
        int DayOffset{ 0 };
        int NightOffset{ 0 };

        /// Daytime level for any display without one of its own. A display
        /// seen for the first time gets its own (see AdoptDisplays), so these
        /// mostly serve displays carried over from before that.
        int DayBrightness{ 100 };

        /// Night-time level for any display without one of its own.
        int NightBrightness{ 30 };

        /// Per-display levels, keyed by device key. A display is written here
        /// when first seen, with levels worked out from the display itself;
        /// absent means "use the defaults above".
        std::map<std::wstring, DisplayLevels> PerDisplay;

        bool FadeEnabled{ false };
        int FadeMinutes{ 30 };

        /// Effective fade window: zero unless fading is switched on.
        int EffectiveFadeMinutes() const { return FadeEnabled ? std::max(0, FadeMinutes) : 0; }

        /// The levels in force for one display: its own if set, otherwise
        /// the defaults. Returned by value, so a caller cannot edit the
        /// settings by accident.
        DisplayLevels LevelsFor(std::wstring const& deviceKey) const
        {
            if (auto found = PerDisplay.find(deviceKey); found != PerDisplay.end())
                return found->second;

            DisplayLevels defaults;
            defaults.Day = DayBrightness;
            defaults.Night = NightBrightness;
            return defaults;
        }
    };
}
