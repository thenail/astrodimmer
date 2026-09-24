#include "pch.h"
#include "SolarTimes.h"

// Written from the equations of NOAA's Solar Calculator (Global Monitoring
// Laboratory, gml.noaa.gov/grad/solcalc), a work of the U.S. federal
// government and so in the public domain. Those equations are in turn a
// simplified form of Jean Meeus, "Astronomical Algorithms".

namespace AstroDimmer::Core::SolarTimes
{
    namespace
    {
        constexpr double Rad = std::numbers::pi / 180.0;

        /// Julian day of the Unix epoch, 1970-01-01T00:00Z.
        constexpr double JulianUnixEpoch = 2440587.5;
        constexpr double JulianJ2000 = 2451545.0;

        /// Zenith angle of the sun's centre at sunrise and sunset: 90 degrees
        /// plus 0.833 for atmospheric refraction and the radius of the disc.
        constexpr double SunriseZenith = 90.833;

        /// Where the sun is as seen from Earth at one instant.
        struct SunPosition
        {
            /// Degrees north of the celestial equator.
            double Declination;

            /// Apparent minus mean solar time, in minutes.
            double EquationOfTime;
        };

        double Wrap360(double degrees)
        {
            degrees = std::fmod(degrees, 360.0);
            return degrees < 0 ? degrees + 360.0 : degrees;
        }

        SunPosition PositionAt(Instant at)
        {
            // Julian centuries since J2000.0.
            double t = (at / MsPerDay + JulianUnixEpoch - JulianJ2000) / 36525.0;

            double meanLongitude = Wrap360(280.46646 + t * (36000.76983 + t * 0.0003032));
            double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
            double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

            double m = meanAnomaly * Rad;
            double centre = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
                + std::sin(2 * m) * (0.019993 - 0.000101 * t)
                + std::sin(3 * m) * 0.000289;

            // Nutation and aberration, through the longitude of the moon's
            // ascending node.
            double omega = (125.04 - 1934.136 * t) * Rad;
            double apparentLongitude = (meanLongitude + centre - 0.00569 - 0.00478 * std::sin(omega)) * Rad;

            double meanObliquity = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
            double obliquity = (meanObliquity + 0.00256 * std::cos(omega)) * Rad;

            double declination = std::asin(std::sin(obliquity) * std::sin(apparentLongitude));

            double y = std::tan(obliquity / 2);
            y *= y;
            double l0 = meanLongitude * Rad;
            double e = eccentricity;
            double equationOfTime = 4 / Rad * (y * std::sin(2 * l0)
                - 2 * e * std::sin(m)
                + 4 * e * y * std::sin(m) * std::cos(2 * l0)
                - 0.5 * y * y * std::sin(4 * l0)
                - 1.25 * e * e * std::sin(2 * m));

            return { declination / Rad, equationOfTime };
        }

        /// Degrees either side of solar noon at which the sun crosses the
        /// horizon; NaN when it never does (polar day or night).
        double HourAngle(double latitude, double declination)
        {
            double phi = latitude * Rad;
            double dec = declination * Rad;
            return std::acos(std::cos(SunriseZenith * Rad) / (std::cos(phi) * std::cos(dec))
                - std::tan(phi) * std::tan(dec)) / Rad;
        }
    }

    GeoPoint SubsolarPoint(Instant at)
    {
        auto sun = PositionAt(at);

        // The sun is overhead where apparent solar time is noon. Apparent
        // solar time is UTC plus the equation of time plus four minutes per
        // degree east, so that longitude falls straight out of it.
        double minutesUtc = (at - std::floor(at / MsPerDay) * MsPerDay) / MsPerMinute;
        double longitude = (720.0 - minutesUtc - sun.EquationOfTime) / 4.0;

        // Into [-180, 180): the map's own range.
        longitude = Wrap360(longitude + 180.0) - 180.0;

        return { sun.Declination, longitude };
    }

    std::optional<SunTimes> Get(Instant at, double latitude, double longitude)
    {
        // The UTC date whose solar noon at this longitude is nearest to the
        // instant given: the day in local mean solar time. That makes the
        // result the event for THIS calendar day wherever the place is.
        Instant day = std::floor(at / MsPerDay + longitude / 360.0) * MsPerDay;

        auto utcAt = [&](double minutes) { return day + minutes * MsPerMinute; };

        // Solar noon, with the equation of time evaluated at noon itself.
        Instant noon = utcAt(720.0 - 4.0 * longitude);
        noon = utcAt(720.0 - 4.0 * longitude - PositionAt(noon).EquationOfTime);

        // Each event is first placed using the sun at noon, then once more
        // using the sun at that first estimate, as NOAA's calculator does:
        // the declination moves enough in six hours to be worth the step.
        auto event = [&](double sign) -> std::optional<Instant> {
            Instant t = noon;
            for (int pass = 0; pass < 2; ++pass)
            {
                auto sun = PositionAt(t);
                double ha = HourAngle(latitude, sun.Declination);
                if (std::isnan(ha))
                    return std::nullopt; // polar day or night
                t = utcAt(720.0 - 4.0 * (longitude + sign * ha) - sun.EquationOfTime);
            }
            return t;
        };

        auto rise = event(+1);
        auto set = event(-1);
        if (!rise || !set)
            return std::nullopt;

        return SunTimes{ *rise, *set };
    }
}
