#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "AstroSettings.h"

namespace AstroDimmer::Core
{
    struct AppSettings;

    /// A display as it reads before AstroDimmer has written anything to it.
    struct ObservedDisplay
    {
        std::wstring DeviceKey;

        /// Brightness, 0-100, as the monitor reports it.
        int Brightness{ 0 };

        /// Contrast, 0-100; nothing when the monitor has no contrast control.
        std::optional<int> Contrast;

        /// Screen diagonal from the EDID; nothing when it does not say.
        std::optional<double> DiagonalInches;
    };

    /// Starting day/night levels for a display seen for the first time.
    ///
    /// Monitors do not report how bright they are in absolute terms - most
    /// leave the EDID luminance fields out entirely - so no nits target can
    /// be turned into a percentage. What can be trusted is relative:
    ///
    ///   - Day is the level the monitor is at now: what its owner has been
    ///     living with, which beats any guess.
    ///   - Night is a fraction of that, smaller for bigger screens, which
    ///     throw more light into a dark room at the same level.
    ///   - Contrast stays where it is, day and night. Lowering it on an LCD
    ///     costs image quality for little dimming, and it is the setting
    ///     people calibrate a panel with.
    DisplayLevels RecommendLevels(ObservedDisplay const& display);

    /// Night as a fraction of day: 0.40 at 24" and below, 0.35 at 27", 0.30
    /// at 32" and above, interpolated between; 0.35 when the size is unknown.
    double NightRatio(std::optional<double> diagonalInches);

    /// The screen diagonal an EDID states (bytes 21-22, in centimetres).
    /// Nothing for a malformed EDID, or one that gives an aspect ratio or no
    /// size - as projectors do.
    std::optional<double> EdidDiagonalInches(std::vector<uint8_t> const& edid);

    /// Records the displays present and seeds levels for any seen for the
    /// first time. Returns whether the settings changed and need saving.
    ///
    /// A display already in PerDisplay keeps its levels. On the first run
    /// over a settings file from before KnownDisplays existed, the displays
    /// present are recorded without seeding: AstroDimmer (or Glimmer) has
    /// been driving them, so their current level may be the night one, and
    /// reading that as "day" would halve the schedule.
    bool AdoptDisplays(AppSettings& settings, std::vector<ObservedDisplay> const& displays);
}
