#pragma once

#include <optional>
#include "DateTime.h"

namespace AstroDimmer::Core::SolarTimes
{
    struct SunTimes
    {
        Instant Sunrise;
        Instant Sunset;
    };

    struct GeoPoint
    {
        double Latitude;
        double Longitude;
    };

    /// Sunrise and sunset for the calendar day containing <paramref name="at"/>,
    /// or nothing when the sun neither rises nor sets that day (polar day or
    /// polar night), where the hour angle is undefined.
    ///
    /// Computed with NOAA's Solar Calculator equations, which are in the
    /// public domain; the tests check them against the U.S. Naval Observatory.
    std::optional<SunTimes> Get(Instant at, double latitude, double longitude);

    /// The point on Earth with the sun directly overhead, in degrees.
    ///
    /// This is what makes the map's day/night overlay possible: everywhere
    /// more than 90 degrees of arc from here is in darkness, so the terminator
    /// can be drawn from this single point without evaluating sunrise for
    /// every longitude. Same solar position as sunrise/sunset, so the
    /// overlay and the schedule cannot disagree about where the sun is.
    GeoPoint SubsolarPoint(Instant at);
}
