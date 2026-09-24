#pragma once

#include <optional>
#include <string>
#include "Event.h"

namespace AstroDimmer::Displays
{
    /// Where a brightness change came from.
    enum class BrightnessOrigin
    {
        /// The user moved a slider. Suppresses the day/night schedule.
        User,

        /// Read back from the display. Must not trigger a write.
        Hardware,

        /// Applied by the day/night schedule. Must not count as manual.
        Schedule,
    };

    /// One attached display.
    ///
    /// The origin of a change matters as much as its value: a user drag
    /// suppresses the schedule for the rest of the period, while the
    /// schedule's own writes must not, and a value read back from the
    /// hardware must not be written straight back to it.
    class DisplayItem
    {
    public:
        std::wstring DeviceKey;

        /// Windows' name for it where it has one, else the model or maker.
        std::wstring Name;

        /// A line to match the row to the panel in front of you, e.g.
        /// "Dell  ·  Display 2  ·  2560 × 1440".
        std::wstring Description;

        /// Brightness goes through the high-level API rather than raw VCP.
        bool UseHighLevel{ false };
        DWORD MaxBrightness{ 100 };

        /// The monitor answered VCP 0x12. Contrast has no high-level API to
        /// fall back on, so a panel silent on that code simply has none -
        /// worth saying rather than offering a slider that writes into the void.
        bool SupportsContrast{ false };
        DWORD MaxContrast{ 100 };

        /// Screen diagonal from the EDID, where it states one.
        std::optional<double> DiagonalInches;

        /// A row fabricated by --simulate-displays: never written to hardware.
        bool IsSimulated{ false };

        int Brightness() const { return m_brightness; }

        /// Sets the level and, unless it came from the hardware, raises
        /// BrightnessChanged so the service writes it out.
        void SetBrightness(int value, BrightnessOrigin origin);

        int Contrast() const { return m_contrast; }

        /// fromHardware records a reading without writing it back.
        void SetContrast(int value, bool fromHardware = false);

        /// Whether the flyout offers a contrast slider for this display.
        bool ShowContrast() const { return m_showContrast; }
        void SetShowContrast(bool value);

        /// For the service: a level to be written.
        Event<DisplayItem&, BrightnessOrigin> BrightnessChanged;
        Event<DisplayItem&> ContrastChanged;

        /// For the UI: something shown about this display changed.
        Event<> Changed;

    private:
        int m_brightness{ 0 };
        int m_contrast{ 0 };
        bool m_showContrast{ false };
    };
}
