#include "pch.h"
#include "Host.h"
#include "CommandLine.h"
#include "DisplayDefaults.h"
#include "Native/DisplayInfo.h"
#include "Native/Shell.h"
#include "Trace.h"

using namespace std::chrono_literals;

namespace AstroDimmer
{
    namespace
    {
        /// See m_uiGoneAt: comfortably longer than a click, far shorter than
        /// a deliberate second one.
        constexpr ULONGLONG ReopenGuardMs = 500;

        /// The monitors Windows has active, as one comparable string; empty
        /// when it cannot say. Cheap - no DDC/CI - so it can be polled.
        std::wstring MonitorSignature()
        {
            std::vector<std::wstring> paths;
            for (auto const& t : Native::DisplayInfo::Targets())
                paths.push_back(t.SourceName + L"|" + t.DevicePath);
            std::sort(paths.begin(), paths.end());

            std::wstring signature;
            for (auto const& p : paths)
                signature += p + L"\n";
            return signature;
        }

        std::wstring Quote(std::wstring const& s)
        {
            return L"\"" + s + L"\"";
        }
    }

    Host::Host(winrt::Windows::System::DispatcherQueue const& dispatcher)
        : m_dispatcher(dispatcher)
    {
        m_settings = Core::AppSettings::LoadDefault();
        m_displays = std::make_unique<Displays::DisplayService>(dispatcher);
        m_astro = std::make_unique<Displays::AstroRunner>(*m_displays, m_settings, dispatcher);
        m_events = std::make_unique<Native::SystemEvents>();
        m_link = std::make_unique<Link::Endpoint>(Link::HostClass, [this](Link::Message const& m) { OnMessage(m); });

        // A user drag suspends the schedule until the next sunrise or sunset.
        m_displays->UserChangedBrightness.Add([this](Displays::DisplayItem& item)
        {
            m_astro->NoteManualChange(item.Brightness(), item.DeviceKey);
        });

        // So does a change made elsewhere - the monitor's own buttons, another
        // app - once a sync has seen it. Otherwise drift re-apply would set
        // the level straight back on the next tick.
        m_displays->ExternalChangedBrightness.Add([this](Displays::DisplayItem& item)
        {
            m_astro->NoteManualChange(item.Brightness(), item.DeviceKey);
        });

        // A display seen for the first time gets starting levels worked out
        // from how it reads now - before anything has been written to it, and
        // before the UI hears of it, so it builds rows from the seeded levels
        // rather than the global defaults.
        m_displays->DisplaysChanged.Add([this]
        {
            AdoptDisplays();

            // Whatever moves a level - the schedule, a sync, a wake - the open
            // windows follow.
            for (auto const& item : m_displays->Displays())
            {
                std::weak_ptr<Displays::DisplayItem> weak = item;
                item->Changed.Add([this, weak]
                {
                    if (auto d = weak.lock(); d && !m_applyingFromUi)
                        SendLevels(*d);
                });
            }

            Send(MakeDisplays());
        });

        auto status = [this](std::wstring const& message)
        {
            m_status = message;
            auto m = Link::Make(Link::Type::Status);
            m.Set(L"text", message);
            Send(m);
        };
        m_displays->StatusChanged.Add(status);
        m_displays->RetryStatus.Add([status](std::wstring const& message)
        {
            Trace::Log(L"displays: " + message);
            status(message);
        });

        m_events->DisplaysChanged = [this] { OnDisplaysChanged(); };

        // The schedule keeps ticking while the screens are off - only the
        // hardware writes are held - so brightness is already right when the
        // panel comes back.
        m_events->DisplayPowerChanged = [this](Native::DisplayPower state)
        {
            Trace::Log(L"display power: " + std::to_wstring(static_cast<int>(state)));
            m_displays->SetDisplayPowerAsync(state != Native::DisplayPower::Off);
        };

        // Sunrise, day, sunset, night: the tray glyph says which, with the
        // same icons the settings page uses for the same three ideas.
        m_tray = std::make_unique<TrayIcon>(StageGlyph(m_astro->Stage())[0], [this] { OnTrayClick(); },
                                            [this] { OnTrayRightClick(); });

        m_astro->StageChanged.Add([this](AstroStage stage)
        {
            Trace::Log(L"stage: " + std::to_wstring(static_cast<int>(stage)));
            m_tray->SetGlyph(StageGlyph(stage)[0]);

            auto m = Link::Make(Link::Type::Stage);
            m.Set(L"stage", static_cast<int>(stage));
            Send(m);
        });

        m_events->ThemeChanged = [this]
        {
            Trace::Log(L"theme changed - repainting tray glyph");
            m_tray->Repaint();
        };

        // Enumerate and start the schedule now, so it runs whether or not the
        // panel is ever opened: a user who never clicks the icon should still
        // get their evening dimming.
        InitializeAsync();

        // --show / --settings: open a window at startup, for measurement and
        // screenshots; reaching Settings otherwise means a real click on the
        // tray, which a screenshot script cannot do.
        using CommandLine::Has;
        if (Has(L"--settings"))
            LaunchUi(L"--settings");
        else if (Has(L"--show"))
            LaunchUi(L"--show");
    }

    Host::~Host()
    {
        if (m_displayPoll) m_displayPoll.Stop();
        if (m_uiWait) UnregisterWaitEx(m_uiWait, INVALID_HANDLE_VALUE);
        if (m_uiProcess) CloseHandle(m_uiProcess);
    }

    winrt::fire_and_forget Host::InitializeAsync()
    {
        m_knownMonitors = MonitorSignature();
        co_await m_displays->RefreshAsync();
        m_astro->Start();
    }

    winrt::fire_and_forget Host::OnDisplaysChanged()
    {
        // Displays were added, removed, or came back from sleep. A hardware
        // change is a fresh situation, so the retry ladder starts over.
        Trace::Log(L"displays changed - re-enumerating");
        m_knownMonitors = MonitorSignature();
        m_displays->ResetRetries();
        co_await m_displays->RefreshAsync();

        // Forced: a monitor that has just appeared is at whatever level it
        // powered on with, and the scheduler would otherwise see an unchanged
        // period and call it settled.
        m_astro->ApplyNow();
    }

    void Host::AdoptDisplays()
    {
        std::vector<Core::ObservedDisplay> observed;
        for (auto const& d : m_displays->Displays())
        {
            if (d->IsSimulated) continue;
            observed.push_back({ d->DeviceKey, d->Brightness(),
                                 d->SupportsContrast ? std::optional<int>(d->Contrast()) : std::nullopt,
                                 d->DiagonalInches });
        }

        if (Core::AdoptDisplays(m_settings, observed))
        {
            Trace::Log(L"known displays updated");
            m_settings.Save();
            SendSettings();
        }
    }

    // ------------------------------------------------------------ tray

    void Host::OnTrayClick()
    {
        switch (m_uiState)
        {
        case UiState::Connected:
        {
            // The click on our own tray icon is what grants the right to take
            // the foreground; it is ours, and the panel is in another process.
            AllowSetForegroundWindow(m_uiProcessId);
            Send(Link::Make(Link::Type::Toggle));
            break;
        }

        case UiState::Starting:
            // Already on its way; a second click would only stack a toggle
            // onto a panel the user has not seen yet.
            break;

        case UiState::None:
            if (GetTickCount64() - m_uiGoneAt < ReopenGuardMs)
            {
                Trace::Log(L"tray: click just closed the panel - not reopening");
                break;
            }
            LaunchUi(L"--show");
            break;
        }
    }

    void Host::OnTrayRightClick()
    {
        switch (m_uiState)
        {
        case UiState::Connected:
            AllowSetForegroundWindow(m_uiProcessId);
            Send(Link::Make(Link::Type::OpenSettings));
            break;
        case UiState::Starting:
            break;
        case UiState::None:
            LaunchUi(L"--settings");
            break;
        }
    }

    // ------------------------------------------------------------ the UI process

    void Host::LaunchUi(wchar_t const* action)
    {
        auto exe = Native::Shell::ExecutablePath();
        std::wstring command = Quote(exe) + L" --ui " + action;

        wchar_t hwnd[32];
        swprintf_s(hwnd, L" --host=%llu", static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(m_link->Hwnd())));
        command += hwnd;

        // What the host was started with that is really the UI's business.
        if (CommandLine::Has(L"--pin")) command += L" --pin";
        for (auto name : { L"--theme", L"--lang" })
            if (auto value = CommandLine::Value(name))
                command += L" " + std::wstring(name) + L"=" + Quote(*value);

        Trace::Log(L"ui: starting (" + std::wstring(action) + L")");

        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                            &process))
        {
            Trace::Log(L"ui: could not start (" + std::to_wstring(GetLastError()) + L")");
            return;
        }

        CloseHandle(process.hThread);

        // Ours to pass on: the click that asked for the panel.
        AllowSetForegroundWindow(process.dwProcessId);

        if (m_uiProcess) CloseHandle(m_uiProcess);
        m_uiProcess = process.hProcess;
        m_uiProcessId = process.dwProcessId;
        m_uiState = UiState::Starting;

        // Its exit, however it comes - a quit, a close, a crash - arrives on
        // the thread pool and is handed back to this thread.
        RegisterWaitForSingleObject(&m_uiWait, m_uiProcess, [](void* context, BOOLEAN)
        {
            auto self = static_cast<Host*>(context);
            self->m_dispatcher.TryEnqueue([self] { self->OnUiGone(); });
        }, this, INFINITE, WT_EXECUTEONLYONCE);
    }

    void Host::OnUiConnected(HWND hwnd)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        // Only the UI this host started. One started by hand finds the host
        // too, but a second copy of the panel would only confuse.
        if (processId != m_uiProcessId)
        {
            Trace::Log(L"ui: ignoring a UI this host did not start");
            return;
        }

        m_uiHwnd = hwnd;
        m_uiState = UiState::Connected;
        Trace::Log(L"ui: connected");

        Send(MakeState());
        StartPolling();
    }

    void Host::OnUiGone()
    {
        if (m_uiWait)
        {
            UnregisterWaitEx(m_uiWait, nullptr);
            m_uiWait = nullptr;
        }
        if (m_uiProcess)
        {
            CloseHandle(m_uiProcess);
            m_uiProcess = nullptr;
        }

        m_uiProcessId = 0;
        m_uiHwnd = nullptr;
        m_uiState = UiState::None;
        m_uiGoneAt = GetTickCount64();
        Trace::Log(L"ui: gone");
    }

    // ------------------------------------------------------------ messages

    void Host::OnMessage(Link::Message const& message)
    {
        auto type = Link::TypeOf(message);

        auto key = [&] { auto v = message.Find(L"key"); return v ? v->AsString().value_or(L"") : L""; };
        auto value = [&] { auto v = message.Find(L"value"); return v ? v->AsInt() : std::nullopt; };

        if (type == Link::Type::Hello)
        {
            auto hwnd = message.Find(L"hwnd");
            if (auto number = hwnd ? hwnd->AsNumber() : std::nullopt)
                OnUiConnected(reinterpret_cast<HWND>(static_cast<UINT_PTR>(*number)));
        }
        else if (type == Link::Type::SetBrightness)
        {
            // The same as a drag used to be in-process: coalesced, and it
            // holds off the schedule.
            if (auto item = m_displays->Find(key()); item && value())
            {
                m_applyingFromUi = true;
                item->SetBrightness(*value(), Displays::BrightnessOrigin::User);
                m_applyingFromUi = false;
            }
        }
        else if (type == Link::Type::SetContrast)
        {
            if (auto item = m_displays->Find(key()); item && value())
            {
                m_applyingFromUi = true;
                item->SetContrast(*value());
                m_applyingFromUi = false;
            }
        }
        else if (type == Link::Type::SaveSettings)
        {
            auto settings = message.Find(L"settings");
            if (!settings) return;

            m_settings = Core::AppSettings::FromJson(*settings);

            // The UI's copy may predate a display that has appeared since;
            // adopting again restores its entry rather than losing it.
            AdoptDisplays();

            m_settings.Save();

            // New values apply at once rather than on the next minute's tick.
            m_astro->SettingsChanged();
        }
        else if (type == Link::Type::Quit)
        {
            Quit();
        }
    }

    void Host::Send(Link::Message const& message)
    {
        if (m_uiState != UiState::Connected) return;
        m_link->Send(m_uiHwnd, message);
    }

    Link::Message Host::MakeDisplays() const
    {
        auto list = Link::Message::MakeArray();
        for (auto const& d : m_displays->Displays())
        {
            auto item = Link::Message::MakeObject();
            item.Set(L"key", d->DeviceKey);
            item.Set(L"name", d->Name);
            item.Set(L"description", d->Description);
            item.Set(L"contrastSupported", d->SupportsContrast);
            item.Set(L"simulated", d->IsSimulated);
            item.Set(L"brightness", d->Brightness());
            item.Set(L"contrast", d->Contrast());
            list.Append(std::move(item));
        }

        auto m = Link::Make(Link::Type::Displays);
        m.Set(L"displays", std::move(list));
        return m;
    }

    Link::Message Host::MakeState() const
    {
        auto m = MakeDisplays();
        m.Set(L"type", Link::Type::State);
        m.Set(L"settings", m_settings.ToJson());
        m.Set(L"stage", static_cast<int>(m_astro->Stage()));
        if (m_status) m.Set(L"status", *m_status);
        return m;
    }

    void Host::SendLevels(Displays::DisplayItem const& item)
    {
        auto m = Link::Make(Link::Type::Levels);
        m.Set(L"key", item.DeviceKey);
        m.Set(L"brightness", item.Brightness());
        m.Set(L"contrast", item.Contrast());
        Send(m);
    }

    void Host::SendSettings()
    {
        auto m = Link::Make(Link::Type::Settings);
        m.Set(L"settings", m_settings.ToJson());
        Send(m);
    }

    // ------------------------------------------------------------ polling

    void Host::StartPolling()
    {
        // Windows' display-change messages do not arrive for every monitor
        // that comes or goes - some docks and KVMs stay silent - so while
        // someone is looking at the displays, the list is also compared
        // every couple of seconds. The comparison is cheap; a full DDC/CI
        // probe happens only when the list has actually changed. Otherwise
        // the levels are read back, so a change made elsewhere shows too.
        if (!m_displayPoll)
        {
            m_displayPoll = m_dispatcher.CreateTimer();
            m_displayPoll.Interval(2s);
            m_displayPoll.Tick([this](auto&&, auto&&) { PollDisplays(); });
        }

        if (!m_displayPoll.IsRunning())
        {
            m_displayPoll.Start();
            PollDisplays();
        }
    }

    void Host::PollDisplays()
    {
        // Nothing open to show a change on: stop until something is.
        if (m_uiState != UiState::Connected)
        {
            m_displayPoll.Stop();
            return;
        }

        auto current = MonitorSignature();
        if (!current.empty() && current != m_knownMonitors)
        {
            Trace::Log(L"display poll: monitor list changed");
            OnDisplaysChanged();
            return;
        }

        // Levels too: something else may have moved them, and the open
        // window should show where the panels really are.
        m_displays->SyncLevelsAsync();
    }

    void Host::Quit()
    {
        Trace::Log(L"quit");

        // The UI is told, not waited for: it closes its windows and goes.
        Send(Link::Make(Link::Type::Quit));

        m_tray.reset();
        PostQuitMessage(0);
    }
}
