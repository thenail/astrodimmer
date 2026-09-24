#include "pch.h"
#include "Native/Shell.h"

namespace AstroDimmer::Native::Shell
{
    namespace
    {
        constexpr wchar_t PersonalizeKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)";
        constexpr wchar_t RunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
        constexpr wchar_t RunValue[] = L"AstroDimmer";

        bool IsDark(wchar_t const* value)
        {
            DWORD data = 1, size = sizeof(data);
            LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, PersonalizeKey, value, RRF_RT_REG_DWORD, nullptr,
                                          &data, &size);
            return status == ERROR_SUCCESS && data == 0;
        }
    }

    bool TaskbarIsDark() { return IsDark(L"SystemUsesLightTheme"); }

    bool AppsAreDark() { return IsDark(L"AppsUseLightTheme"); }

    std::wstring ExecutablePath()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length < path.size())
            {
                path.resize(length);
                return path;
            }
            path.resize(path.size() * 2);
        }
    }

    bool StartupEnabled()
    {
        wchar_t command[2048]{};
        DWORD size = sizeof(command);
        if (RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, command, &size) != ERROR_SUCCESS)
            return false;

        std::wstring haystack{ command };
        std::wstring needle = ExecutablePath();
        auto lower = [](std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; };
        return lower(haystack).find(lower(needle)) != std::wstring::npos;
    }

    bool SetStartupEnabled(bool enabled)
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
            return false;

        LSTATUS status;
        if (enabled)
        {
            std::wstring command = L"\"" + ExecutablePath() + L"\"";
            status = RegSetValueExW(key, RunValue, 0, REG_SZ, reinterpret_cast<BYTE const*>(command.c_str()),
                                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            status = RegDeleteValueW(key, RunValue);
            if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
        }

        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
}
