#pragma once

#include <memory>
#include "Displays/AstroRunner.h"
#include "Displays/DisplayService.h"
#include "Link.h"
#include "Native/SystemEvents.h"
#include "Settings.h"
#include "TrayIcon.h"

namespace AstroDimmer
{
    /// The process that lives in the tray: the settings, the displays, the
    /// schedule, the system notifications and the icon. No WinUI - that is
    /// the point of it; see Link.h.
    ///
    /// Owns the settings file: the UI sends its edits here rather than
    /// writing them itself, so there is one writer and nothing to race.
    class Host
    {
    public:
        explicit Host(winrt::Windows::System::DispatcherQueue const& dispatcher);
        ~Host();

        Host(Host const&) = delete;
        Host& operator=(Host const&) = delete;

    private:
        enum class UiState { None, Starting, Connected };

        winrt::fire_and_forget InitializeAsync();
        winrt::fire_and_forget OnDisplaysChanged();
        void AdoptDisplays();

        void OnTrayClick();
        void OnTrayRightClick();

        /// Starts AstroDimmer.exe --ui, which opens with this action.
        void LaunchUi(wchar_t const* action);
        void OnUiConnected(HWND hwnd);
        void OnUiGone();

        void OnMessage(Link::Message const& message);
        void Send(Link::Message const& message);
        Link::Message MakeState() const;
        Link::Message MakeDisplays() const;
        void SendLevels(Displays::DisplayItem const& item);
        void SendSettings();

        void StartPolling();
        void PollDisplays();

        void Quit();

        winrt::Windows::System::DispatcherQueue m_dispatcher;

        Core::AppSettings m_settings;
        std::unique_ptr<Displays::DisplayService> m_displays;
        std::unique_ptr<Displays::AstroRunner> m_astro;
        std::unique_ptr<Native::SystemEvents> m_events;
        std::unique_ptr<TrayIcon> m_tray;
        std::unique_ptr<Link::Endpoint> m_link;

        /// The last word on the display list, as the panel shows it while it
        /// has no rows; nothing until the first enumeration has finished.
        std::optional<std::wstring> m_status;

        UiState m_uiState{ UiState::None };
        HWND m_uiHwnd{};
        HANDLE m_uiProcess{};
        DWORD m_uiProcessId{};
        HANDLE m_uiWait{};

        /// When the UI process last went away. A click on the tray icon while
        /// the panel is open first takes focus from the panel, which closes
        /// it and ends the process - and the click itself arrives just after.
        /// It meant "close", so it must not start a new UI.
        ULONGLONG m_uiGoneAt{ 0 };

        /// Set while a change the UI sent is being applied, so it is not
        /// echoed straight back and does not fight a slider mid-drag.
        bool m_applyingFromUi{ false };

        /// Polls Windows' monitor list while a window is open.
        winrt::Windows::System::DispatcherQueueTimer m_displayPoll{ nullptr };

        /// The monitor list as Windows reported it at the last enumeration.
        std::wstring m_knownMonitors;
    };
}
