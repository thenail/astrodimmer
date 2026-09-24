#include "pch.h"
#include "Strings.h"
#include "Trace.h"

#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

namespace AstroDimmer::Strings
{
    namespace
    {
        using winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader;

        /// One loader for the process. It reads AstroDimmer.pri beside the
        /// exe, which the build compiles from the .resw files.
        ResourceLoader const& Loader()
        {
            static ResourceLoader loader = []
            {
                try
                {
                    return ResourceLoader();
                }
                catch (winrt::hresult_error const& e)
                {
                    Trace::Log(L"strings: no resource file (" + std::wstring(e.message()) + L")");
                    return ResourceLoader{ nullptr };
                }
            }();
            return loader;
        }
    }

    std::wstring Get(wchar_t const* key)
    {
        if (auto const& loader = Loader())
        {
            try
            {
                auto value = loader.GetString(key);
                if (!value.empty())
                    return std::wstring(value);
            }
            catch (winrt::hresult_error const&)
            {
            }
        }

        Trace::Log(std::wstring(L"strings: missing ") + key);
        return key;
    }

    std::wstring Format(wchar_t const* key, std::initializer_list<std::wstring_view> args)
    {
        auto text = Get(key);

        size_t index = 0;
        for (auto arg : args)
        {
            auto token = L"{" + std::to_wstring(index++) + L"}";
            for (size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + arg.size()))
                text.replace(at, token.size(), arg);
        }
        return text;
    }

    std::wstring Time(int minutesOfDay)
    {
        SYSTEMTIME time{};
        time.wYear = 2000;
        time.wMonth = 1;
        time.wDay = 1;
        time.wHour = static_cast<WORD>(minutesOfDay / 60);
        time.wMinute = static_cast<WORD>(minutesOfDay % 60);

        wchar_t buffer[64]{};
        if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, buffer, 64) > 0)
            return buffer;

        swprintf_s(buffer, L"%02d:%02d", minutesOfDay / 60, minutesOfDay % 60);
        return buffer;
    }
}
