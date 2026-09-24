// Ported from Glimmer.Tests/SolarTimesParityTests.cs and SubsolarPointTests.cs,
// with the parity reference changed from suncalc to the Naval Observatory.

#include "Test.h"
#include "SolarTimes.h"

using namespace AstroDimmer::Core;

namespace
{
    Instant Utc(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
    {
        return FromUtc({ y, mo, d, h, mi, s });
    }

    /// Differential check against an independent authority.
    ///
    /// Expected values are the U.S. Naval Observatory's published times
    /// (aa.usno.navy.mil) for these exact inputs, which are whole minutes, so
    /// the tolerance is a minute: half for their rounding, half for ours. The
    /// engine quantises to whole minutes anyway.
    ///
    /// These replaced suncalc's output as the reference: suncalc runs about
    /// 70 seconds late throughout (its J0 term), and missed USNO by a minute
    /// in seven of these ten times.
    void CheckAgainstUsno(Instant noon, double lat, double lon, Instant rise, Instant set)
    {
        auto actual = SolarTimes::Get(noon, lat, lon);
        CHECK(actual.has_value());
        CHECK_NEAR(rise, actual->Sunrise, MsPerMinute);
        CHECK_NEAR(set, actual->Sunset, MsPerMinute);
    }
}

TEST(Matches_the_naval_observatory_within_a_minute)
{
    CheckAgainstUsno(Utc(2026, 6, 21, 12), 59.3293, 18.0686, Utc(2026, 6, 21, 1, 31), Utc(2026, 6, 21, 20, 8));
    CheckAgainstUsno(Utc(2026, 12, 21, 12), 59.3293, 18.0686, Utc(2026, 12, 21, 7, 43), Utc(2026, 12, 21, 13, 48));
    CheckAgainstUsno(Utc(2026, 3, 15, 12), 40.7128, -74.0060, Utc(2026, 3, 15, 11, 8), Utc(2026, 3, 15, 23, 3));
    CheckAgainstUsno(Utc(2026, 9, 22, 12), -33.8688, 151.2093, Utc(2026, 9, 21, 19, 45), Utc(2026, 9, 22, 7, 51));
    CheckAgainstUsno(Utc(2026, 1, 5, 12), 51.5074, -0.1278, Utc(2026, 1, 5, 8, 5), Utc(2026, 1, 5, 16, 7));
}

// ------------------------------------------------------------ subsolar point

TEST(Declination_reaches_the_tropics_at_the_solstices)
{
    CHECK_RANGE(23.44 - 1, 23.44 + 1, SolarTimes::SubsolarPoint(Utc(2026, 6, 21, 12)).Latitude);
    CHECK_RANGE(-23.44 - 1, -23.44 + 1, SolarTimes::SubsolarPoint(Utc(2026, 12, 21, 12)).Latitude);
}

TEST(Declination_crosses_the_equator_at_the_equinoxes)
{
    CHECK_RANGE(-1, 1, SolarTimes::SubsolarPoint(Utc(2026, 3, 20, 12)).Latitude);
    CHECK_RANGE(-1, 1, SolarTimes::SubsolarPoint(Utc(2026, 9, 22, 12)).Latitude);
}

TEST(Noon_utc_puts_the_sun_near_greenwich)
{
    // Within the equation of time, which peaks around 16 minutes, or four degrees.
    CHECK_RANGE(-5, 5, SolarTimes::SubsolarPoint(Utc(2026, 6, 21, 12)).Longitude);
}

TEST(The_sun_moves_west_at_fifteen_degrees_an_hour)
{
    auto noon = Utc(2026, 6, 21, 12);

    double before = SolarTimes::SubsolarPoint(noon).Longitude;
    double after = SolarTimes::SubsolarPoint(noon + 6 * MsPerHour).Longitude;

    CHECK_RANGE(89, 91, before - after);
}

TEST(The_point_always_lands_on_the_map)
{
    auto t = Utc(2026, 1, 1);

    // Every three hours for a year: enough to cross both the longitude wrap
    // and every season.
    for (int i = 0; i < 365 * 8; ++i, t += 3 * MsPerHour)
    {
        auto p = SolarTimes::SubsolarPoint(t);
        CHECK_RANGE(-23.5, 23.5, p.Latitude);
        CHECK_RANGE(-180, 180, p.Longitude);
    }
}

TEST(Local_times_are_converted_to_utc)
{
    // A local wall-clock time names the same instant once converted; without
    // that the overlay would be wrong by the machine's offset.
    auto utc = Utc(2026, 6, 21, 12);
    auto local = ToLocal(utc);

    CHECK_NEAR(SolarTimes::SubsolarPoint(utc).Longitude, SolarTimes::SubsolarPoint(FromLocal(local)).Longitude, 1e-6);
}

// ------------------------------------------------------------ time

TEST(Local_and_utc_round_trip)
{
    auto utc = Utc(2026, 6, 21, 12, 34, 56);
    CHECK_EQ(utc, FromLocal(ToLocal(utc)));
}

TEST(Calendar_arithmetic_crosses_month_and_year_ends)
{
    DateTime newYearsEve{ 2025, 12, 31, 23, 30 };
    auto next = newYearsEve.AddMinutes(45);

    CHECK_EQ(2026, next.Year);
    CHECK_EQ(1, next.Month);
    CHECK_EQ(1, next.Day);
    CHECK_EQ(0, next.Hour);
    CHECK_EQ(15, next.Minute);

    DateTime leap{ 2028, 2, 28, 12 };
    CHECK_EQ(29, leap.AddHours(24).Day);
}
