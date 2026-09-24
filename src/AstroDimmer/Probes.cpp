#include "pch.h"
#include "Probes.h"
#include "FlyoutWindow.xaml.h"
#include "DisplayDefaults.h"
#include "Json.h"
#include "Native/DdcSession.h"
#include "Native/DisplayInfo.h"
#include "Native/Shell.h"

#include <Psapi.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

namespace AstroDimmer::Probes
{
    namespace
    {
        std::wstring BesideExe(wchar_t const* name)
        {
            auto exe = Native::Shell::ExecutablePath();
            return exe.substr(0, exe.find_last_of(L'\\') + 1) + name;
        }

        void WriteText(wchar_t const* name, std::wstring const& text)
        {
            auto bytes = Core::Json::ToUtf8(text);
            HANDLE file = CreateFileW(BesideExe(name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
        }

        std::wstring Hex(DWORD value)
        {
            wchar_t buffer[16];
            swprintf_s(buffer, L"0x%08X", value);
            return buffer;
        }

        struct Memory
        {
            size_t PrivateMb;
            size_t WorkingSetMb;
        };

        Memory SampleMemory()
        {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters));
            return { counters.PrivateUsage / (1024 * 1024), counters.WorkingSetSize / (1024 * 1024) };
        }

        std::wstring Trim(std::wstring const& s)
        {
            return s.size() <= 300 ? s : s.substr(0, 300) + L"... (" + std::to_wstring(s.size()) + L" chars)";
        }
    }

    void RunDdcProbe(bool includeWrites, std::function<void()> done)
    {
        std::thread([includeWrites, done]
        {
            std::wstring report;
            auto line = [&](std::wstring const& s = L"") { report += s + L"\r\n"; };

            SYSTEMTIME now{};
            GetLocalTime(&now);
            wchar_t stamp[32];
            swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour,
                       now.wMinute, now.wSecond);
            line(std::wstring(L"AstroDimmer DDC/CI probe - ") + stamp);
            line(std::wstring(60, L'='));

            Native::DdcSession session;

            for (bool accurate : { false, true })
            {
                line();
                line(accurate ? L"--- refresh (validation = Accurate) ---" : L"--- refresh (validation = Fast) ---");

                ULONGLONG started = GetTickCount64();
                session.Refresh(accurate, false, true);
                auto monitors = session.List();
                line(L"took " + std::to_wstring(GetTickCount64() - started) + L" ms, found " +
                     std::to_wstring(monitors.size()) + L" monitor(s)");

                bool anySupported = false;
                for (auto const& m : monitors)
                {
                    anySupported |= m.DdcciSupported;
                    line();
                    line(L"  name         : " + m.Name);
                    line(L"  deviceName   : " + m.DeviceName);
                    line(L"  description  : " + m.Description);
                    line(L"  deviceKey    : " + m.DeviceKey);
                    line(L"  devicePath   : " + m.DevicePath);
                    line(L"  matchedBy    : " + m.MatchedBy);
                    line(std::wstring(L"  ddcciSupported=") + (m.DdcciSupported ? L"True" : L"False") +
                         L" hlBrightness=" + (m.HighLevelBrightnessSupported ? L"True" : L"False"));

                    // VCP 0x10 is the standard brightness code.
                    if (auto vcp = session.GetVcp(m.DeviceKey, 0x10))
                        line(L"  VCP 0x10     : current=" + std::to_wstring(vcp->Current) + L" max=" +
                             std::to_wstring(vcp->Max));
                    else
                        line(L"  VCP 0x10     : FAILED (" + Hex(session.LastErrorCode()) + L") " +
                             session.LastErrorMessage());

                    if (m.HighLevelBrightnessSupported)
                    {
                        auto hl = session.GetHighLevelBrightness(m.DeviceKey);
                        line(hl ? L"  high-level   : current=" + std::to_wstring(hl->Current) + L" min=" +
                                      std::to_wstring(hl->Min) + L" max=" + std::to_wstring(hl->Max)
                                : L"  high-level   : FAILED");
                    }

                    auto caps = session.GetCapabilities(m.DeviceKey);
                    line(L"  capabilities : " + (!caps || caps->empty() ? std::wstring(L"<none>") : Trim(*caps)));

                    // What a first sighting would seed, and the size behind
                    // it: an unknown size falls back to the middle ratio.
                    auto edid = Native::DisplayInfo::Edid(m.DeviceKey);
                    auto inches = edid ? Core::EdidDiagonalInches(*edid) : std::nullopt;
                    line(L"  diagonal     : " + (inches ? std::to_wstring(std::lround(*inches * 10) / 10) + L"." +
                                                              std::to_wstring(std::lround(*inches * 10) % 10) + L"\""
                                                        : std::wstring(edid ? L"not in EDID" : L"no EDID")));

                    if (auto brightness = session.GetVcp(m.DeviceKey, 0x10); brightness && brightness->Max)
                    {
                        auto contrast = session.GetVcp(m.DeviceKey, 0x12);
                        auto percent = [](Native::VcpReading r) { return static_cast<int>(std::lround(r.Current * 100.0 / r.Max)); };
                        auto levels = Core::RecommendLevels({ m.DeviceKey, percent(*brightness),
                                                              contrast && contrast->Max ? std::optional<int>(percent(*contrast))
                                                                                        : std::nullopt,
                                                              inches });
                        line(L"  would seed   : day " + std::to_wstring(levels.Day) + L", night " +
                             std::to_wstring(levels.Night) + L", contrast " + std::to_wstring(levels.DayContrast) +
                             (contrast ? L"" : L" (default; no contrast control)"));
                    }
                }

                // The expensive accurate pass is only worth running when the
                // fast one found nothing to talk to.
                if (anySupported)
                    break;
            }

            line();
            if (includeWrites)
            {
                line(L"--- write test (brightness nudge, then restore) ---");
                for (auto const& m : session.List())
                {
                    if (!m.DdcciSupported) continue;

                    auto before = session.GetVcp(m.DeviceKey, 0x10);
                    if (!before)
                    {
                        line(L"  " + m.Name + L": cannot read brightness, skipping");
                        continue;
                    }

                    // Small, reversible nudge; the original is restored either way.
                    DWORD target = before->Current > before->Max / 2 ? before->Current - 5 : before->Current + 5;
                    bool set = session.SetVcp(m.DeviceKey, 0x10, target);
                    Sleep(400);
                    auto after = session.GetVcp(m.DeviceKey, 0x10);
                    bool restored = session.SetVcp(m.DeviceKey, 0x10, before->Current);

                    line(L"  " + m.Name + L": " + std::to_wstring(before->Current) + L" -> " + std::to_wstring(target) +
                         L" (set=" + (set ? L"True" : L"False") + L", readback=" +
                         (after ? std::to_wstring(after->Current) : std::wstring(L"?")) + L", restored=" +
                         (restored ? L"True" : L"False") + L")");
                }
            }
            else
            {
                line(L"(read-only; pass --ddc-write to test setting brightness)");
            }

            session.Close();
            WriteText(L"ddc-probe.txt", report);
            done();
        }).detach();
    }

    void RunMemoryCycle(winrt::com_ptr<winrt::AstroDimmer::implementation::FlyoutWindow> const& flyout,
                        bool destroyOnHide, std::function<void()> done)
    {
        struct Step
        {
            std::wstring Label;
            std::function<void()> Do;
            int WaitSeconds;
        };

        auto log = std::make_shared<std::wstring>();
        auto sample = [log](std::wstring const& label)
        {
            auto m = SampleMemory();
            wchar_t buffer[128];
            swprintf_s(buffer, L"%-26s private=%5zu MB   ws=%5zu MB\r\n", label.c_str(), m.PrivateMb, m.WorkingSetMb);
            *log += buffer;
            WriteText(L"memory-cycle.txt", *log);
        };

        auto hide = [flyout, destroyOnHide]
        {
            if (destroyOnHide) flyout->Close();
            else flyout->HideFlyout();
        };

        auto steps = std::make_shared<std::deque<Step>>();
        steps->push_back({ L"after show #1", [flyout] { flyout->ShowFlyout(); }, 8 });
        steps->push_back({ destroyOnHide ? L"after DESTROY" : L"after hide #1", hide, 8 });
        steps->push_back({ L"after settle", [] {}, 10 });

        // A destroyed window cannot be shown again.
        if (!destroyOnHide)
        {
            steps->push_back({ L"after show #2", [flyout] { flyout->ShowFlyout(); }, 8 });
            steps->push_back({ L"after hide #2", hide, 8 });
            steps->push_back({ L"after hide #2 settle", [] {}, 10 });
        }

        sample(L"start (never shown)");

        auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.Interval(std::chrono::seconds(2));
        auto pendingLabel = std::make_shared<std::optional<std::wstring>>();

        timer.Tick([steps, sample, done, pendingLabel](auto&& sender, auto&&)
        {
            // Sampled at the END of each wait, once the render thread has settled.
            if (*pendingLabel)
            {
                sample(**pendingLabel);
                pendingLabel->reset();
            }

            if (steps->empty())
            {
                sender.Stop();
                done();
                return;
            }

            auto step = std::move(steps->front());
            steps->pop_front();
            step.Do();
            *pendingLabel = step.Label;
            sender.Interval(std::chrono::seconds(step.WaitSeconds));
        });
        timer.Start();

        // Kept alive for the run: a released timer stops firing.
        winrt::detach_abi(timer);
    }

    void WriteDiagnostics()
    {
        using namespace winrt::Microsoft::UI::Composition::SystemBackdrops;

        OSVERSIONINFOEXW version{ sizeof(version) };
        auto rtlGetVersion = reinterpret_cast<LONG(WINAPI*)(OSVERSIONINFOEXW*)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (rtlGetVersion) rtlGetVersion(&version);

        DWORD transparency = 1, size = sizeof(transparency);
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);

        std::wstring lines;
        lines += L"command line        : " + std::wstring(GetCommandLineW()) + L"\r\n";
        lines += L"OS build            : " + std::to_wstring(version.dwBuildNumber) + L"\r\n";
        lines += std::wstring(L"acrylic supported   : ") + (DesktopAcrylicController::IsSupported() ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"mica supported      : ") + (MicaController::IsSupported() ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"transparency effects: ") + (transparency ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"taskbar dark        : ") + (Native::Shell::TaskbarIsDark() ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"apps dark           : ") + (Native::Shell::AppsAreDark() ? L"True" : L"False") + L"\r\n";
        WriteText(L"diagnostics.txt", lines);

        // Sampled again once things have settled.
        auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.Interval(std::chrono::seconds(12));
        timer.IsRepeating(false);
        timer.Tick([lines](auto&&, auto&&) mutable
        {
            auto m = SampleMemory();
            lines += L"--- after 12s ---\r\n";
            lines += L"private bytes       : " + std::to_wstring(m.PrivateMb) + L" MB\r\n";
            lines += L"working set         : " + std::to_wstring(m.WorkingSetMb) + L" MB\r\n";
            WriteText(L"diagnostics.txt", lines);
        });
        timer.Start();


        // Deliberately leaked: a one-shot aid, and a WinRT object in a static
        // would be torn down after WinUI itself, at exit.
        winrt::detach_abi(timer);
    }
}
