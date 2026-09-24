#include "pch.h"
#include "DisplayNames.h"

namespace AstroDimmer::Core::DisplayNames
{
    namespace
    {
        std::wstring Upper(std::wstring text)
        {
            for (auto& c : text) c = static_cast<wchar_t>(towupper(c));
            return text;
        }

        std::wstring Trim(std::wstring const& text)
        {
            auto first = text.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) return {};
            auto last = text.find_last_not_of(L" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        /// PnP vendor IDs, assigned by UEFI. Only the common monitor makers.
        std::map<std::wstring, std::wstring> const& Vendors()
        {
            static const std::map<std::wstring, std::wstring> vendors{
                { L"AAC", L"AOC" }, { L"ACI", L"ASUS" }, { L"ACR", L"Acer" },
                { L"AOC", L"AOC" }, { L"APP", L"Apple" }, { L"AUO", L"AU Optronics" },
                { L"AUS", L"ASUS" }, { L"BNQ", L"BenQ" }, { L"BOE", L"BOE" },
                { L"CMN", L"Chi Mei" }, { L"CMO", L"Chi Mei" }, { L"DEL", L"Dell" },
                { L"ENC", L"EIZO" }, { L"EIZ", L"EIZO" }, { L"GSM", L"LG" },
                { L"HPN", L"HP" }, { L"HWP", L"HP" }, { L"IVM", L"iiyama" },
                { L"LEN", L"Lenovo" }, { L"LGD", L"LG Display" }, { L"MSI", L"MSI" },
                { L"NEC", L"NEC" }, { L"PHL", L"Philips" }, { L"SAM", L"Samsung" },
                { L"SDC", L"Samsung" }, { L"SEC", L"Samsung" }, { L"SHP", L"Sharp" },
                { L"SNY", L"Sony" }, { L"VSC", L"ViewSonic" },
            };
            return vendors;
        }
    }

    std::optional<std::wstring> Vendor(std::wstring const& deviceKey)
    {
        auto upper = Upper(deviceKey);
        auto i = upper.find(L"DISPLAY#");
        if (i == std::wstring::npos) return std::nullopt;

        auto rest = upper.substr(i + 8);
        if (rest.size() < 3) return std::nullopt;

        auto code = rest.substr(0, 3);
        if (!std::all_of(code.begin(), code.end(), [](wchar_t c) { return iswalpha(c); }))
            return std::nullopt;

        auto found = Vendors().find(code);
        return found != Vendors().end() ? found->second : code;
    }

    std::optional<int> DisplayNumber(std::wstring const& deviceName)
    {
        auto upper = Upper(deviceName);
        auto i = upper.rfind(L"DISPLAY");
        if (i == std::wstring::npos) return std::nullopt;

        auto digits = upper.substr(i + 7);
        while (!digits.empty() && digits.back() == L'\\') digits.pop_back();

        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return iswdigit(c); }))
            return std::nullopt;

        return _wtoi(digits.c_str());
    }

    bool IsGenericName(std::wstring const& name)
    {
        static const wchar_t* generic[] = {
            L"GENERIC PNP MONITOR",
            L"GENERIC NON-PNP MONITOR",
            L"DEFAULT MONITOR",
            L"PNP-MONITOR (STANDARD)",
        };

        auto upper = Upper(Trim(name));
        return std::any_of(std::begin(generic), std::end(generic), [&](auto g) { return upper == g; });
    }

    std::optional<std::wstring> ModelFromCapabilities(std::wstring const& capabilities)
    {
        auto upper = Upper(capabilities);
        auto start = upper.find(L"MODEL(");
        if (start == std::wstring::npos) return std::nullopt;

        start += 6;
        auto end = capabilities.find(L')', start);
        if (end == std::wstring::npos) return std::nullopt;

        auto model = Trim(capabilities.substr(start, end - start));
        if (model.empty()) return std::nullopt;
        return model;
    }

    std::wstring Resolve(std::optional<std::wstring> const& friendlyName, std::wstring const& capabilities,
                         std::wstring const& deviceName, std::wstring const& deviceKey,
                         std::wstring const& displayFormat)
    {
        // Windows' own name first. It costs no bus traffic, cannot time out,
        // and is the string Settings and Quick Settings show - so the panel
        // says the same thing on every run, which a capability read over I2C
        // (intermittently empty) cannot promise.
        if (friendlyName && !friendlyName->empty() && !IsGenericName(*friendlyName))
            return Trim(*friendlyName);

        if (auto model = ModelFromCapabilities(capabilities))
            return *model;

        // Still better than a device path: "Dell Display 2" names the maker
        // and the number Windows puts on the monitor in its own settings.
        auto vendor = Vendor(deviceKey);
        auto number = DisplayNumber(deviceName);
        if (number)
        {
            auto display = displayFormat;
            if (auto at = display.find(L"{0}"); at != std::wstring::npos)
                display.replace(at, 3, std::to_wstring(*number));
            return vendor ? *vendor + L" " + display : display;
        }

        return deviceName;
    }
}
