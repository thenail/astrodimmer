#include "pch.h"
#include "DayTimeline.h"
#include "AstroEngine.h"

namespace AstroDimmer::Core::DayTimeline
{
    using AstroEngine::MinutesPerDay;

    std::vector<Segment> DaySegments(int dayStart, int nightStart)
    {
        if (dayStart == nightStart)
            return {};

        if (dayStart < nightStart)
            return { { dayStart, nightStart - dayStart } };

        // Night begins earlier on the clock than day, so daylight is what is
        // left at both ends of the strip.
        return {
            { 0, nightStart },
            { dayStart, MinutesPerDay - dayStart },
        };
    }

    std::vector<BandPiece> SplitBand(int start, int length)
    {
        if (length <= 0)
            return {};

        length = std::min(length, MinutesPerDay);

        int begin = AstroEngine::WrapMinutes(start);
        int overflow = begin + length - MinutesPerDay;

        if (overflow <= 0)
            return { { begin, length, 0 } };

        return {
            { begin, MinutesPerDay - begin, 0 },
            { 0, overflow, MinutesPerDay - begin },
        };
    }
}
