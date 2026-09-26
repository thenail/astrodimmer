#include "pch.h"
#include "App.xaml.h"
#include "CommandLine.h"
#include "Host.h"
#include "Probes.h"
#include "Strings.h"
#include "Trace.h"

#include <DispatcherQueue.h>
#pragma comment(lib, "CoreMessaging.lib")

namespace
{
    /// The panel and Settings. What XAML would generate as the entry point,
    /// written out: the generated one, renamed under DISABLE_XAML_GENERATED_MAIN,
    /// decides whether to create the App by a check C++/WinRT types fail, and
    /// then starts XAML with no application at all.
    int RunUi()
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
        {
            winrt::make<winrt::AstroDimmer::implementation::App>();
        });
        return 0;
    }

    void PumpMessages()
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    /// The tray process: a Win32 message loop and the dispatcher queue
    /// Windows itself provides - not WinUI's, which would load WinUI.
    int RunHost()
    {
        using AstroDimmer::CommandLine::Has;

        winrt::init_apartment(winrt::apartment_type::single_threaded);

        DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        winrt::Windows::System::DispatcherQueueController controller{ nullptr };
        winrt::check_hresult(CreateDispatcherQueueController(
            options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));

        // --ddc / --ddc-write: hardware probe, then exit. Before the trace is
        // touched, so probing beside a running instance leaves its log alone.
        if (Has(L"--ddc") || Has(L"--ddc-write"))
        {
            DWORD thread = GetCurrentThreadId();
            AstroDimmer::Probes::RunDdcProbe(Has(L"--ddc-write"), [thread] { PostThreadMessageW(thread, WM_QUIT, 0, 0); });
            PumpMessages();
            return 0;
        }

        // One host per session: a second launch - the Start menu entry
        // clicked again, sign-in beside a copy started by hand - just exits.
        // Before the trace is touched, so the running instance keeps its log.
        // The handle stays open for the life of the process.
        CreateMutexW(nullptr, FALSE, L"Local\\AstroDimmer.Host");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;

        AstroDimmer::Trace::Clear();
        AstroDimmer::Trace::Log(std::wstring(L"startup: ") + GetCommandLineW());

        AstroDimmer::Strings::UseDisplayLanguage();
        AstroDimmer::Probes::WriteDiagnostics();

        {
            AstroDimmer::Host host(controller.DispatcherQueue());
            PumpMessages();
        }

        // Anything still queued runs down before the thread goes.
        DWORD thread = GetCurrentThreadId();
        controller.ShutdownQueueAsync().Completed([thread](auto&&, auto&&) { PostThreadMessageW(thread, WM_QUIT, 0, 0); });
        PumpMessages();
        return 0;
    }
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // --ui: the panel and Settings, started by the host when they are asked
    // for. See Link.h for why they are a process of their own.
    if (AstroDimmer::CommandLine::Has(L"--ui"))
        return RunUi();

    return RunHost();
}
