#pragma once

#include <functional>

namespace AstroDimmer::Native
{
    /// The console display's power state, from GUID_CONSOLE_DISPLAY_STATE.
    enum class DisplayPower
    {
        Off = 0,
        On = 1,

        /// Dimmed ahead of turning off, on hardware that does it. The panel is
        /// still lit and still answers DDC, so this counts as on.
        Dimmed = 2,
    };

    /// The system notifications AstroDimmer reacts to, on one hidden window.
    ///
    ///   - Displays changed. Unplugging, docking or waking from sleep leaves
    ///     stale monitor handles behind, so the display list has to be
    ///     rebuilt. Windows sends several WM_DISPLAYCHANGE messages for one
    ///     physical event, and re-probing DDC/CI per message would be slow and
    ///     hard on the monitors, so bursts are collapsed into one callback.
    ///   - Display power. Never write DDC/CI to a sleeping panel: a write to a
    ///     monitor in DPMS off is at best pointless and at worst wakes it -
    ///     exactly what a user who walked away does not want. An asleep
    ///     monitor does not answer DDC either, so Windows' own notification is
    ///     the only reliable source.
    ///   - Theme changed. The tray glyph is drawn for one taskbar colour and
    ///     outlives any switch between light and dark.
    ///
    /// A real top-level window, never shown, rather than a message-only one:
    /// message-only windows do not receive broadcasts, and WM_DISPLAYCHANGE
    /// and WM_SETTINGCHANGE are broadcasts.
    class SystemEvents
    {
    public:
        SystemEvents();
        ~SystemEvents();

        SystemEvents(SystemEvents const&) = delete;
        SystemEvents& operator=(SystemEvents const&) = delete;

        /// Raised once a burst of display changes has settled.
        std::function<void()> DisplaysChanged;

        /// Raised when the display power state changes - including the wake,
        /// which is the cue to flush anything that was held back.
        std::function<void(DisplayPower)> DisplayPowerChanged;

        /// Raised when Windows switches between light and dark.
        std::function<void()> ThemeChanged;

        /// Starts optimistic: Windows delivers the real value right after
        /// registration, and until it does, treating the display as on only
        /// risks a write that would have happened anyway.
        DisplayPower CurrentPower() const { return m_power; }

        bool PowerNotificationsRegistered() const { return m_powerRegistration != nullptr; }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
        void ScheduleDisplaysChanged();

        HWND m_hwnd{};
        HPOWERNOTIFY m_powerRegistration{};
        DisplayPower m_power{ DisplayPower::On };
    };
}
