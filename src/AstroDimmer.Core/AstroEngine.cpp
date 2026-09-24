#include "pch.h"
#include "AstroEngine.h"
#include "SolarTimes.h"

namespace AstroDimmer::Core::AstroEngine
{
    namespace
    {
        int ClampOffset(int offset) { return std::clamp(offset, -OffsetLimit, OffsetLimit); }

        int MinMax(int value) { return std::clamp(value, 0, 100); }

        double Lerp(double start, double finish, double t) { return start * (1 - t) + finish * t; }
    }

    int WrapMinutes(double minutes)
    {
        // Away from zero, as the original's Math.Round(.., AwayFromZero).
        int m = static_cast<int>(std::round(minutes));
        return ((m % MinutesPerDay) + MinutesPerDay) % MinutesPerDay;
    }

    std::optional<Boundaries> GetBoundaries(double latitude, double longitude, int dayOffset,
                                            int nightOffset, DateTime const& localNow)
    {
        // 0,0 means "unset" in settings, not Null Island.
        if (std::isnan(latitude) || std::isnan(longitude))
            return std::nullopt;
        if (latitude == 0 && longitude == 0)
            return std::nullopt;

        auto times = SolarTimes::Get(FromLocal(localNow), latitude, longitude);
        if (!times)
            return std::nullopt;

        // Boundaries are compared against a local wall clock, so the solar
        // times must be expressed in the same frame.
        auto sunrise = ToLocal(times->Sunrise);
        auto sunset = ToLocal(times->Sunset);

        int dayBase = sunrise.MinuteOfDay();
        int nightBase = sunset.MinuteOfDay();

        int dOff = ClampOffset(dayOffset);
        int nOff = ClampOffset(nightOffset);

        return Boundaries{
            dayBase,
            nightBase,
            dOff,
            nOff,
            WrapMinutes(dayBase + dOff),
            WrapMinutes(nightBase + nOff),
        };
    }

    std::optional<State> GetState(double latitude, double longitude, int dayOffset, int nightOffset,
                                  int fadeMinutes, DateTime const& localNow)
    {
        auto b = GetBoundaries(latitude, longitude, dayOffset, nightOffset, localNow);
        if (!b)
            return std::nullopt;

        if (b->DayStart == b->NightStart)
            return std::nullopt;

        int now = localNow.MinuteOfDay();

        Period period = b->DayStart < b->NightStart
            ? (now >= b->DayStart && now < b->NightStart ? Period::Day : Period::Night)
            // Night starts earlier on the clock than day, so day wraps midnight.
            : (now >= b->NightStart && now < b->DayStart ? Period::Night : Period::Day);

        int span = std::max(0, fadeMinutes);
        int sinceBoundary = WrapMinutes(now - (period == Period::Day ? b->DayStart : b->NightStart));
        double progress = span > 0 ? std::min(1.0, static_cast<double>(sinceBoundary) / span) : 1.0;

        return State{ *b, period, progress, sinceBoundary };
    }

    std::optional<Period> GetPeriod(double latitude, double longitude, int dayOffset, int nightOffset,
                                    DateTime const& localNow)
    {
        auto state = GetState(latitude, longitude, dayOffset, nightOffset, 0, localNow);
        if (!state)
            return std::nullopt;
        return state->Current;
    }

    int GetBrightness(Period period, int dayBrightness, int nightBrightness)
    {
        return MinMax(period == Period::Day ? dayBrightness : nightBrightness);
    }

    int GetTargetBrightness(State const& state, int dayBrightness, int nightBrightness)
    {
        int target = GetBrightness(state.Current, dayBrightness, nightBrightness);
        if (state.Progress >= 1)
            return target;

        int previous = GetBrightness(state.Current == Period::Day ? Period::Night : Period::Day,
                                     dayBrightness, nightBrightness);

        return MinMax(static_cast<int>(std::round(Lerp(previous, target, state.Progress))));
    }
}
