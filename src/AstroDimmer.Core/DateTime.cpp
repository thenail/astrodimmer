#include "pch.h"
#include "DateTime.h"

namespace AstroDimmer::Core
{
    namespace
    {
        // Howard Hinnant's civil-date algorithms: days since 1970-01-01 for a
        // proleptic Gregorian date, and back. Exact for every date SYSTEMTIME
        // can hold.
        std::int64_t DaysFromCivil(std::int64_t y, unsigned m, unsigned d)
        {
            y -= m <= 2;
            const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
        }

        void CivilFromDays(std::int64_t z, int& year, int& month, int& day)
        {
            z += 719468;
            const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            const unsigned doe = static_cast<unsigned>(z - era * 146097);
            const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
            const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const unsigned mp = (5 * doy + 2) / 153;
            day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
            month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
            year = static_cast<int>(y + (month <= 2));
        }

        DateTime FromNaiveMs(double ms)
        {
            // Rounded to whole milliseconds first, so a value a hair under a
            // minute boundary does not come back one minute early.
            auto total = static_cast<std::int64_t>(std::llround(ms));
            std::int64_t days = total / 86'400'000;
            std::int64_t rest = total % 86'400'000;
            if (rest < 0)
            {
                rest += 86'400'000;
                --days;
            }

            DateTime value;
            CivilFromDays(days, value.Year, value.Month, value.Day);
            value.Hour = static_cast<int>(rest / 3'600'000);
            value.Minute = static_cast<int>(rest / 60'000 % 60);
            value.Second = static_cast<int>(rest / 1000 % 60);
            value.Millisecond = static_cast<int>(rest % 1000);
            return value;
        }

        SYSTEMTIME ToSystemTime(DateTime const& value)
        {
            SYSTEMTIME st{};
            st.wYear = static_cast<WORD>(value.Year);
            st.wMonth = static_cast<WORD>(value.Month);
            st.wDay = static_cast<WORD>(value.Day);
            st.wHour = static_cast<WORD>(value.Hour);
            st.wMinute = static_cast<WORD>(value.Minute);
            st.wSecond = static_cast<WORD>(value.Second);
            st.wMilliseconds = static_cast<WORD>(value.Millisecond);
            return st;
        }

        DateTime FromSystemTime(SYSTEMTIME const& st)
        {
            return { st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds };
        }
    }

    double NaiveMs(DateTime const& value)
    {
        return static_cast<double>(DaysFromCivil(value.Year, static_cast<unsigned>(value.Month),
                                                 static_cast<unsigned>(value.Day))) * MsPerDay
             + value.Hour * MsPerHour
             + value.Minute * MsPerMinute
             + value.Second * 1000.0
             + value.Millisecond;
    }

    DateTime DateTime::Date() const
    {
        return { Year, Month, Day };
    }

    DateTime DateTime::AddMinutes(double minutes) const
    {
        return FromNaiveMs(NaiveMs(*this) + minutes * MsPerMinute);
    }

    Instant FromUtc(DateTime const& utc)
    {
        return NaiveMs(utc);
    }

    DateTime ToUtc(Instant instant)
    {
        return FromNaiveMs(instant);
    }

    Instant FromLocal(DateTime const& local)
    {
        // The dynamic zone, so a year's own daylight-saving dates are used
        // rather than this year's - the same rules .NET's local time follows.
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        GetDynamicTimeZoneInformation(&zone);

        SYSTEMTIME localTime = ToSystemTime(local);
        SYSTEMTIME utcTime{};
        if (!TzSpecificLocalTimeToSystemTimeEx(&zone, &localTime, &utcTime))
            return NaiveMs(local);

        // SYSTEMTIME drops nothing below a millisecond, so this is exact.
        return NaiveMs(FromSystemTime(utcTime));
    }

    DateTime ToLocal(Instant instant)
    {
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        GetDynamicTimeZoneInformation(&zone);

        SYSTEMTIME utcTime = ToSystemTime(ToUtc(instant));
        SYSTEMTIME localTime{};
        if (!SystemTimeToTzSpecificLocalTimeEx(&zone, &utcTime, &localTime))
            return ToUtc(instant);

        return FromSystemTime(localTime);
    }

    Instant Now()
    {
        FILETIME ft{};
        GetSystemTimePreciseAsFileTime(&ft);

        ULARGE_INTEGER ticks{};
        ticks.LowPart = ft.dwLowDateTime;
        ticks.HighPart = ft.dwHighDateTime;

        // FILETIME counts 100 ns ticks from 1601-01-01.
        constexpr std::uint64_t EpochDifference = 116'444'736'000'000'000ULL;
        return static_cast<double>(ticks.QuadPart - EpochDifference) / 10'000.0;
    }

    DateTime LocalNow()
    {
        return ToLocal(Now());
    }
}
