#include "pch.h"
#include "App.xaml.h"
#include "CommandLine.h"
#include "DisplayDefaults.h"
#include "Probes.h"
#include "SettingsWindow.xaml.h"
#include "Trace.h"
#include "Native/DisplayInfo.h"

#include <winrt/Microsoft.Windows.Globalization.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace Native = ::AstroDimmer::Native;
namespace Displays = ::AstroDimmer::Displays;

namespace winrt::AstroDimmer::implementation
{
    App::App()
    {
        // --theme=light|dark: look at a theme without switching the desktop.
        // Must be set before any resources load.
        if (auto theme = ::AstroDimmer::CommandLine::Value(L"--theme"))
        {
            if (_wcsicmp(theme->c_str(), L"dark") == 0) RequestedTheme(ApplicationTheme::Dark);
            if (_wcsicmp(theme->c_str(), L"light") == 0) RequestedTheme(ApplicationTheme::Light);
        }

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        using ::AstroDimmer::CommandLine::Has;

        // A tray app has no main window: closing (or never showing) a window
        // must not end the process. Only Quit does.
        DispatcherShutdownMode(DispatcherShutdownMode::OnExplicitShutdown);

        auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        // --ddc / --ddc-write: hardware probe, then exit. Before the trace is
        // touched, so probing beside a running instance leaves its log alone.
        if (Has(L"--ddc") || Has(L"--ddc-write"))
        {
            ::AstroDimmer::Probes::RunDdcProbe(Has(L"--ddc-write"), [dispatcher]
            {
                dispatcher.TryEnqueue([] { Application::Current().Exit(); });
            });
            return;
        }

        ::AstroDimmer::Trace::Clear();
        ::AstroDimmer::Trace::Log(std::wstring(L"startup: ") + GetCommandLineW());

        // The language Windows' own menus are in. Left alone, the resources
        // would follow the preferred-language list instead, which can differ
        // (English Windows, Swedish list) and leave the app looking foreign
        // next to everything else. Where there is no translation, the list
        // and then English are still the fallbacks.
        //
        // --lang=de: another language, for checking a translation without
        // switching the whole machine.
        //
        // Before any window is built: each resolves its strings as it loads.
        auto language = ::AstroDimmer::CommandLine::Value(L"--lang");
        if (!language)
        {
            wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
            if (LCIDToLocaleName(GetUserDefaultUILanguage(), name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
                language = name;
        }
        if (language)
        {
            ::AstroDimmer::Trace::Log(L"language: " + *language);
            Microsoft::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(*language);
        }

        m_services = std::make_unique<::AstroDimmer::Services>();
        auto& services = *m_services;
        ::AstroDimmer::Services::Set(&services);

        services.Settings = ::AstroDimmer::Core::AppSettings::LoadDefault();
        services.Displays = std::make_unique<Displays::DisplayService>(dispatcher);
        services.Astro = std::make_unique<Displays::AstroRunner>(*services.Displays, services.Settings, dispatcher);
        services.Events = std::make_unique<Native::SystemEvents>();

        services.OpenSettings = [this] { OpenSettings(); };
        services.Quit = [this] { Quit(); };

        // A user drag suspends the schedule until the next sunrise or sunset.
        services.Displays->UserChangedBrightness.Add([&services](Displays::DisplayItem& item)
        {
            services.Astro->NoteManualChange(item.Brightness(), item.DeviceKey);
        });

        // A display seen for the first time gets starting levels worked out
        // from how it reads now - before anything has been written to it, and
        // ahead of the windows' own handlers, so they build rows from the
        // seeded levels rather than the global defaults.
        services.Displays->DisplaysChanged.Add([&services]
        {
            std::vector<::AstroDimmer::Core::ObservedDisplay> observed;
            for (auto const& d : services.Displays->Displays())
            {
                if (d->IsSimulated) continue;
                observed.push_back({ d->DeviceKey, d->Brightness(),
                                     d->SupportsContrast ? std::optional<int>(d->Contrast()) : std::nullopt,
                                     d->DiagonalInches });
            }

            if (::AstroDimmer::Core::AdoptDisplays(services.Settings, observed))
            {
                ::AstroDimmer::Trace::Log(L"known displays updated");
                services.Settings.Save();
            }
        });

        // New values apply at once rather than on the next minute's tick.
        services.SettingsChanged.Add([&services] { services.Astro->SettingsChanged(); });

        services.Events->DisplaysChanged = [this] { OnDisplaysChanged(); };

        // The schedule keeps ticking while the screens are off - only the
        // hardware writes are held - so brightness is already right when the
        // panel comes back.
        services.Events->DisplayPowerChanged = [&services](Native::DisplayPower state)
        {
            ::AstroDimmer::Trace::Log(L"display power: " + std::to_wstring(static_cast<int>(state)));
            services.Displays->SetDisplayPowerAsync(state != Native::DisplayPower::Off);
        };

        // Created once and kept, so opening the panel is a show, not a build.
        m_flyout = make_self<FlyoutWindow>();

        // --pin keeps the panel up when it loses focus, for inspection.
        m_flyout->Pin(Has(L"--pin"));

        // Sunrise, day, sunset, night: the tray glyph says which, with the
        // same icons the settings page uses for the same three ideas.
        m_tray = std::make_unique<::AstroDimmer::TrayIcon>(
            ::AstroDimmer::StageGlyph(services.Astro->Stage())[0],
            [this] { m_flyout->Toggle(); WatchDisplays(); },
            [this] { OpenSettings(); });

        services.Astro->StageChanged.Add([this](::AstroDimmer::AstroStage stage)
        {
            ::AstroDimmer::Trace::Log(L"stage: " + std::to_wstring(static_cast<int>(stage)));
            m_tray->SetGlyph(::AstroDimmer::StageGlyph(stage)[0]);
        });

        services.Events->ThemeChanged = [this]
        {
            ::AstroDimmer::Trace::Log(L"theme changed - repainting tray glyph");
            m_tray->Repaint();
        };

        ::AstroDimmer::Probes::WriteDiagnostics();

        // Enumerate and start the schedule now, so it runs whether or not the
        // panel is ever opened: a user who never clicks the icon should still
        // get their evening dimming.
        InitializeAsync();

        // --show: open the panel at startup, for measurement and screenshots.
        if (Has(L"--show"))
        {
            m_flyout->ShowFlyout();
            WatchDisplays();
        }

        // --settings: open Settings at startup; reaching it otherwise means a
        // real click on the tray, which a screenshot script cannot do.
        if (Has(L"--settings"))
            OpenSettings();

        // --cycle [--destroy]: show/hide the panel and sample memory, then exit.
        if (Has(L"--cycle"))
            ::AstroDimmer::Probes::RunMemoryCycle(m_flyout, Has(L"--destroy"), [this] { Quit(); });
    }

    namespace
    {
        /// The monitors Windows has active, as one comparable string; empty
        /// when it cannot say. Cheap - no DDC/CI - so it can be polled.
        std::wstring MonitorSignature()
        {
            std::vector<std::wstring> paths;
            for (auto const& t : ::AstroDimmer::Native::DisplayInfo::Targets())
                paths.push_back(t.SourceName + L"|" + t.DevicePath);
            std::sort(paths.begin(), paths.end());

            std::wstring signature;
            for (auto const& p : paths)
                signature += p + L"\n";
            return signature;
        }
    }

    fire_and_forget App::InitializeAsync()
    {
        m_knownMonitors = MonitorSignature();
        co_await m_services->Displays->RefreshAsync();
        m_services->Astro->Start();
    }

    fire_and_forget App::OnDisplaysChanged()
    {
        // Displays were added, removed, or came back from sleep. A hardware
        // change is a fresh situation, so the retry ladder starts over.
        ::AstroDimmer::Trace::Log(L"displays changed - re-enumerating");
        m_knownMonitors = MonitorSignature();
        m_services->Displays->ResetRetries();
        co_await m_services->Displays->RefreshAsync();

        // Forced: a monitor that has just appeared is at whatever level it
        // powered on with, and the scheduler would otherwise see an unchanged
        // period and call it settled.
        m_services->Astro->ApplyNow();
    }

    void App::WatchDisplays()
    {
        // Windows' display-change messages do not arrive for every monitor
        // that comes or goes - some docks and KVMs stay silent - so while
        // someone is looking at the displays, the list is also compared
        // every couple of seconds. Only the comparison is polled; DDC/CI
        // is probed only when the list has actually changed.
        if (!m_displayPoll)
        {
            m_displayPoll = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_displayPoll.Interval(std::chrono::seconds(2));
            m_displayPoll.Tick([this](auto&&, auto&&) { PollDisplays(); });
        }

        if (!m_displayPoll.IsRunning())
        {
            m_displayPoll.Start();
            PollDisplays();
        }
    }

    void App::PollDisplays()
    {
        // Nothing open to show a change on: stop until something is.
        if (!m_flyout->IsOpen() && !m_settings)
        {
            m_displayPoll.Stop();
            return;
        }

        auto current = MonitorSignature();
        if (!current.empty() && current != m_knownMonitors)
        {
            ::AstroDimmer::Trace::Log(L"display poll: monitor list changed");
            OnDisplaysChanged();
        }
    }

    void App::OpenSettings()
    {
        m_flyout->HideFlyout();

        // Reuse the open window rather than stacking copies - and bring it
        // back if it was minimised, the state that made asking again look
        // like nothing happened.
        if (m_settings)
        {
            m_settings.RestoreAndActivate();
            return;
        }

        m_settings = AstroDimmer::SettingsWindow();
        m_settings.Closed([this](auto&&, auto&&) { m_settings = nullptr; });
        m_settings.Activate();
        WatchDisplays();
    }

    void App::Quit()
    {
        ::AstroDimmer::Trace::Log(L"quit");

        m_tray.reset();
        if (m_settings) m_settings.Close();
        m_flyout->Close();

        Exit();
    }
}
