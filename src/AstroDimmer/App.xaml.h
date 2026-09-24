#pragma once

#include "App.xaml.g.h"
#include "FlyoutWindow.xaml.h"
#include "Services.h"
#include "TrayIcon.h"

namespace winrt::AstroDimmer::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        fire_and_forget InitializeAsync();
        fire_and_forget OnDisplaysChanged();
        void WatchDisplays();
        void PollDisplays();
        void OpenSettings();
        void Quit();

        std::unique_ptr<::AstroDimmer::Services> m_services;
        com_ptr<FlyoutWindow> m_flyout;
        AstroDimmer::SettingsWindow m_settings{ nullptr };
        std::unique_ptr<::AstroDimmer::TrayIcon> m_tray;

        /// Polls Windows' monitor list while the panel or Settings is open.
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_displayPoll{ nullptr };

        /// The monitor list as Windows reported it at the last enumeration.
        std::wstring m_knownMonitors;
    };
}
