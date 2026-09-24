#pragma once

#include <cstdint>

namespace AstroDimmer::Core
{
    /// A point on the UTC time line, in milliseconds since 1970-01-01T00:00Z.
    ///
    /// A double rather than an integer: the solar maths works in fractional
    /// Julian days and hands back fractional milliseconds, and rounding them
    /// here would lose precision for no gain.
    using Instant = double;

    constexpr double MsPerMinute = 60'000.0;
    constexpr double MsPerHour = 3'600'000.0;
    constexpr double MsPerDay = 86'400'000.0;

    /// A calendar date and wall-clock time, with no time zone attached.
    ///
    /// Whether it means local time or UTC is decided by which conversion it is
    /// passed to - the same arrangement as an unspecified .NET DateTime, which
    /// is what the schedule was written against. Arithmetic on it is plain
    /// calendar arithmetic, ignoring daylight saving, again as .NET does.
    struct DateTime
    {
        int Year{ 1970 };
        int Month{ 1 };
        int Day{ 1 };
        int Hour{ 0 };
        int Minute{ 0 };
        int Second{ 0 };
        int Millisecond{ 0 };

        /// Midnight at the start of the same day.
        DateTime Date() const;

        DateTime AddMinutes(double minutes) const;
        DateTime AddHours(double hours) const { return AddMinutes(hours * 60); }

        /// Minutes past midnight, ignoring seconds.
        int MinuteOfDay() const { return Hour * 60 + Minute; }

        bool operator==(DateTime const&) const = default;
    };

    /// Reads the fields as UTC.
    Instant FromUtc(DateTime const& utc);

    /// The UTC calendar fields of an instant.
    DateTime ToUtc(Instant instant);

    /// Reads the fields as local time, using Windows' own time zone rules.
    Instant FromLocal(DateTime const& local);

    /// The local calendar fields of an instant.
    DateTime ToLocal(Instant instant);

    Instant Now();
    DateTime LocalNow();

    /// Fields read as if they were UTC, for plain calendar arithmetic and
    /// differences between two wall-clock times.
    double NaiveMs(DateTime const& value);
}
