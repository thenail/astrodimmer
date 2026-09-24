// Ported from Glimmer.Tests/AstroEngineTests.cs.
//
// The astro engine is pure and clock-injectable, so its behaviour is pinned
// here rather than discovered in the field.

#include "Test.h"
#include "AstroEngine.h"
#include "SolarTimes.h"

using namespace AstroDimmer::Core;
using AstroEngine::Period;

namespace
{
    // Stockholm - high enough latitude that summer and winter differ sharply.
    constexpr double Lat = 59.3293;
    constexpr double Lon = 18.0686;

    DateTime At(int y, int mo, int d, int h, int mi) { return { y, mo, d, h, mi }; }
}

TEST(WrapMinutes_always_lands_in_a_single_day)
{
    CHECK_EQ(0, AstroEngine::WrapMinutes(0));
    CHECK_EQ(0, AstroEngine::WrapMinutes(1440));
    CHECK_EQ(1, AstroEngine::WrapMinutes(1441));
    CHECK_EQ(1439, AstroEngine::WrapMinutes(-1));
    CHECK_EQ(0, AstroEngine::WrapMinutes(-1440));
    CHECK_EQ(1439, AstroEngine::WrapMinutes(-1441));
    CHECK_EQ(0, AstroEngine::WrapMinutes(2880));
}

TEST(Coordinates_of_zero_mean_unset_not_null_island)
{
    CHECK(!AstroEngine::GetBoundaries(0, 0, 0, 0, At(2026, 6, 21, 12, 0)));
}

TEST(Boundaries_are_produced_for_a_normal_location)
{
    auto b = AstroEngine::GetBoundaries(Lat, Lon, 0, 0, At(2026, 6, 21, 12, 0));

    CHECK(b.has_value());
    CHECK_RANGE(0, 1439, b->DayStart);
    CHECK_RANGE(0, 1439, b->NightStart);
    // Midsummer in Stockholm: the sun is up well before it sets.
    CHECK(b->DayBase < b->NightBase);
}

TEST(Offsets_are_clamped_to_the_documented_limit)
{
    auto at = At(2026, 6, 21, 12, 0);
    CHECK_EQ(180, AstroEngine::GetBoundaries(Lat, Lon, 500, 0, at)->DayOffset);
    CHECK_EQ(-180, AstroEngine::GetBoundaries(Lat, Lon, -500, 0, at)->DayOffset);
    CHECK_EQ(45, AstroEngine::GetBoundaries(Lat, Lon, 45, 0, at)->DayOffset);
}

TEST(Offsets_shift_the_boundary_by_exactly_that_many_minutes)
{
    auto at = At(2026, 6, 21, 12, 0);
    auto plain = *AstroEngine::GetBoundaries(Lat, Lon, 0, 0, at);
    auto shifted = *AstroEngine::GetBoundaries(Lat, Lon, 60, -30, at);

    CHECK_EQ(AstroEngine::WrapMinutes(plain.DayStart + 60), shifted.DayStart);
    CHECK_EQ(AstroEngine::WrapMinutes(plain.NightStart - 30), shifted.NightStart);
}

TEST(Polar_day_yields_no_boundaries)
{
    // Longyearbyen at midsummer: the sun never sets.
    CHECK(!AstroEngine::GetBoundaries(78.22, 15.63, 0, 0, At(2026, 6, 21, 12, 0)));
}

TEST(Polar_night_yields_no_boundaries)
{
    CHECK(!AstroEngine::GetBoundaries(78.22, 15.63, 0, 0, At(2026, 12, 21, 12, 0)));
}

TEST(Midday_is_day_and_midnight_is_night)
{
    auto day = AstroEngine::GetPeriod(Lat, Lon, 0, 0, At(2026, 6, 21, 12, 0));
    auto night = AstroEngine::GetPeriod(Lat, Lon, 0, 0, At(2026, 12, 21, 23, 30));

    CHECK(day == Period::Day);
    CHECK(night == Period::Night);
}

TEST(Progress_is_one_when_fading_is_disabled)
{
    auto s = AstroEngine::GetState(Lat, Lon, 0, 0, 0, At(2026, 6, 21, 12, 0));
    CHECK_EQ(1.0, s->Progress);
}

TEST(Progress_ramps_across_the_fade_window_then_holds_at_one)
{
    auto at = At(2026, 6, 21, 12, 0);
    auto b = *AstroEngine::GetBoundaries(Lat, Lon, 0, 0, at);

    // Half way through a 60-minute fade that began at sunrise.
    auto half = at.Date().AddMinutes(b.DayStart + 30);
    auto after = at.Date().AddMinutes(b.DayStart + 90);

    CHECK_NEAR(0.5, AstroEngine::GetState(Lat, Lon, 0, 0, 60, half)->Progress, 0.001);
    CHECK_EQ(1.0, AstroEngine::GetState(Lat, Lon, 0, 0, 60, after)->Progress);
}

TEST(Target_brightness_blends_from_the_outgoing_level)
{
    auto at = At(2026, 6, 21, 12, 0);
    auto b = *AstroEngine::GetBoundaries(Lat, Lon, 0, 0, at);
    auto half = at.Date().AddMinutes(b.DayStart + 30);

    auto s = *AstroEngine::GetState(Lat, Lon, 0, 0, 60, half);

    // Half way from night (30) to day (100).
    CHECK_EQ(65, AstroEngine::GetTargetBrightness(s, 100, 30));
}

TEST(Target_brightness_is_the_plain_level_once_the_fade_completes)
{
    auto s = *AstroEngine::GetState(Lat, Lon, 0, 0, 0, At(2026, 6, 21, 12, 0));
    CHECK_EQ(100, AstroEngine::GetTargetBrightness(s, 100, 30));
}

TEST(Brightness_is_clamped_to_0_100)
{
    CHECK_EQ(100, AstroEngine::GetBrightness(Period::Day, 150, 50));
    CHECK_EQ(0, AstroEngine::GetBrightness(Period::Day, -20, 50));
}

TEST(Sunrise_precedes_sunset_and_both_fall_on_the_requested_day)
{
    auto t = SolarTimes::Get(FromUtc({ 2026, 6, 21, 12, 0 }), Lat, Lon);

    CHECK(t.has_value());
    CHECK(t->Sunrise < t->Sunset);
    CHECK_EQ(21, ToUtc(t->Sunrise).Day);
}
