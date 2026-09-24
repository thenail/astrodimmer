#pragma once

#include <optional>
#include "DateTime.h"

/// Day/night brightness scheduling, driven by sunrise and sunset.
///
/// A branch-for-branch port of Glimmer's engine, which was itself a port of
/// the original JavaScript prototype. Behaviour is deliberately identical,
/// including the edge cases:
///
///   - lat/long of exactly 0,0 is treated as "not configured", not as a valid
///     point in the Atlantic.
///   - Offsets are clamped to +/-180 minutes.
///   - Boundaries wrap across midnight, so night can begin before day on the
///     clock.
///   - Identical day/night boundaries mean "cannot be determined", not a
///     zero-length period.
///
/// Pure and clock-injectable: every function takes the moment to evaluate.
namespace AstroDimmer::Core::AstroEngine
{
    constexpr int MinutesPerDay = 1440;
    constexpr int OffsetLimit = 180;

    /// Where day and night begin, in minutes past local midnight.
    struct Boundaries
    {
        int DayBase;
        int NightBase;
        int DayOffset;
        int NightOffset;
        int DayStart;
        int NightStart;
    };

    enum class Period
    {
        Day,
        Night,
    };

    /// Where the current moment sits in the cycle. Progress is how far a fade
    /// into the period has advanced, and is 1 when fading is off or complete.
    struct State
    {
        Boundaries Bounds;
        Period Current;
        double Progress;
        int SinceBoundary;
    };

    /// ((round(m) mod 1440) + 1440) mod 1440 - always lands in [0, 1440).
    int WrapMinutes(double minutes);

    /// Day and night boundaries for the given local time, or nothing when
    /// they cannot be determined (no coordinates, or polar day/night).
    std::optional<Boundaries> GetBoundaries(double latitude, double longitude, int dayOffset,
                                            int nightOffset, DateTime const& localNow);

    /// Current period and fade progress, or nothing when boundaries are unknown.
    std::optional<State> GetState(double latitude, double longitude, int dayOffset, int nightOffset,
                                  int fadeMinutes, DateTime const& localNow);

    /// Whether the sun currently places us in day or night.
    std::optional<Period> GetPeriod(double latitude, double longitude, int dayOffset, int nightOffset,
                                    DateTime const& localNow);

    /// Configured brightness for a period, clamped to 0-100.
    int GetBrightness(Period period, int dayBrightness, int nightBrightness);

    /// Brightness to apply now. Blends from the outgoing level to the
    /// incoming one across the fade window, returning the incoming level
    /// outright once the fade is done or if fading is off.
    int GetTargetBrightness(State const& state, int dayBrightness, int nightBrightness);
}
