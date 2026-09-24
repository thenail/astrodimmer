#include "pch.h"
#include "DisplayDefaults.h"
#include "Settings.h"

namespace AstroDimmer::Core
{
    namespace
    {
        /// Below this, the current level is more likely a leftover than a
        /// preference, and a night level derived from it would be near black.
        constexpr int MinimumDay = 20;

        struct RatioPoint
        {
            double Inches;
            double Ratio;
        };

        constexpr RatioPoint RatioCurve[] = { { 24, 0.40 }, { 27, 0.35 }, { 32, 0.30 } };
        constexpr double UnknownSizeRatio = 0.35;
    }

    double NightRatio(std::optional<double> diagonalInches)
    {
        if (!diagonalInches || *diagonalInches <= 0)
            return UnknownSizeRatio;

        double inches = *diagonalInches;
        if (inches <= RatioCurve[0].Inches)
            return RatioCurve[0].Ratio;

        for (size_t i = 1; i < std::size(RatioCurve); ++i)
        {
            auto const& a = RatioCurve[i - 1];
            auto const& b = RatioCurve[i];
            if (inches <= b.Inches)
                return a.Ratio + (b.Ratio - a.Ratio) * (inches - a.Inches) / (b.Inches - a.Inches);
        }

        return std::end(RatioCurve)[-1].Ratio;
    }

    DisplayLevels RecommendLevels(ObservedDisplay const& display)
    {
        DisplayLevels levels;
        levels.Day = std::clamp(display.Brightness, MinimumDay, 100);
        levels.Night = static_cast<int>(std::lround(levels.Day * NightRatio(display.DiagonalInches)));

        if (display.Contrast)
        {
            levels.DayContrast = std::clamp(*display.Contrast, 0, 100);
            levels.NightContrast = levels.DayContrast;
        }

        return levels;
    }

    std::optional<double> EdidDiagonalInches(std::vector<uint8_t> const& edid)
    {
        static constexpr uint8_t Header[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
        if (edid.size() < 128 || !std::equal(std::begin(Header), std::end(Header), edid.begin()))
            return std::nullopt;

        // Both zero: no size given. One zero: EDID 1.4's aspect-ratio form.
        double width = edid[21], height = edid[22];
        if (width == 0 || height == 0)
            return std::nullopt;

        return std::sqrt(width * width + height * height) / 2.54;
    }

    bool AdoptDisplays(AppSettings& settings, std::vector<ObservedDisplay> const& displays)
    {
        // An empty probe says nothing about what is attached; the upgrade
        // step has to wait for one that found the monitors.
        if (displays.empty())
            return false;

        bool changed = false;
        for (auto const& display : displays)
        {
            if (!settings.KnownDisplays.insert(display.DeviceKey).second)
                continue;
            changed = true;

            if (settings.TracksKnownDisplays && !settings.Astro.PerDisplay.contains(display.DeviceKey))
                settings.Astro.PerDisplay[display.DeviceKey] = RecommendLevels(display);
        }

        if (!settings.TracksKnownDisplays)
        {
            settings.TracksKnownDisplays = true;
            changed = true;
        }

        return changed;
    }
}
