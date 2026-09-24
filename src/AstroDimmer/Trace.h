#pragma once

// A plain text log next to the exe (trace.txt), for the measurements this
// proof of concept exists to take: how long the flyout takes to appear, and
// what the monitors answered over DDC/CI.
namespace AstroDimmer::Trace
{
    inline std::mutex& Lock()
    {
        static std::mutex lock;
        return lock;
    }

    inline std::wstring Path()
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring path{ exe };
        return path.substr(0, path.find_last_of(L'\\') + 1) + L"trace.txt";
    }

    inline void Clear()
    {
        DeleteFileW(Path().c_str());
    }

    inline void Log(std::wstring const& message)
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);

        wchar_t stamp[32]{};
        swprintf_s(stamp, L"%02d:%02d:%02d.%03d  ",
                   now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        std::wstring line = stamp + message + L"\r\n";
        int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                        nullptr, 0, nullptr, nullptr);
        std::string utf8(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                            utf8.data(), bytes, nullptr, nullptr);

        std::scoped_lock guard{ Lock() };
        HANDLE file = CreateFileW(Path().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;

        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

    /// Milliseconds on a monotonic clock, for measuring intervals.
    inline double NowMs()
    {
        static LARGE_INTEGER frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart * 1000.0 / frequency.QuadPart;
    }
}
