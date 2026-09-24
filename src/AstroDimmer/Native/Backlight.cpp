#include "pch.h"
#include "Native/Backlight.h"

namespace AstroDimmer::Native
{
    namespace
    {
        /// WMI answers in milliseconds on a working machine; this only stops
        /// a wedged provider from holding the DDC thread for good.
        constexpr long QueryTimeoutMs = 5000;

        /// WmiSetBrightness's first argument. What drivers do with it varies;
        /// 1 is what Windows' own tools pass.
        constexpr long SetTimeoutSeconds = 1;

        struct Bstr
        {
            BSTR Value;
            explicit Bstr(wchar_t const* text) : Value(SysAllocString(text)) {}
            ~Bstr() { SysFreeString(Value); }
            Bstr(Bstr const&) = delete;
            Bstr& operator=(Bstr const&) = delete;
            operator BSTR() const { return Value; }
        };

        struct Variant : VARIANT
        {
            Variant() { VariantInit(this); }
            ~Variant() { VariantClear(this); }
            Variant(Variant const&) = delete;
            Variant& operator=(Variant const&) = delete;
        };

        std::optional<int> Integer(VARIANT const& v)
        {
            switch (v.vt)
            {
            case VT_UI1: return v.bVal;
            case VT_I2: return v.iVal;
            case VT_I4: return v.lVal;
            case VT_UI4: return static_cast<int>(v.ulVal);
            default: return std::nullopt;
            }
        }

        std::vector<int> Bytes(VARIANT const& v)
        {
            std::vector<int> values;
            if (v.vt != (VT_ARRAY | VT_UI1) || !v.parray) return values;

            LONG lower = 0, upper = -1;
            SafeArrayGetLBound(v.parray, 1, &lower);
            SafeArrayGetUBound(v.parray, 1, &upper);

            BYTE* data = nullptr;
            if (FAILED(SafeArrayAccessData(v.parray, reinterpret_cast<void**>(&data)))) return values;
            for (LONG i = 0; i <= upper - lower; ++i)
                values.push_back(data[i]);
            SafeArrayUnaccessData(v.parray);
            return values;
        }

        /// A string key in a WMI object path, quoted and escaped.
        std::wstring Quote(std::wstring const& value)
        {
            std::wstring quoted = L"\"";
            for (wchar_t c : value)
            {
                if (c == L'\\' || c == L'"') quoted += L'\\';
                quoted += c;
            }
            return quoted + L"\"";
        }

        std::wstring Hex(HRESULT hr)
        {
            wchar_t buffer[16];
            swprintf_s(buffer, L"0x%08X", static_cast<unsigned>(hr));
            return buffer;
        }
    }

    bool Backlight::Connect()
    {
        if (m_services) return true;

        winrt::com_ptr<IWbemLocator> locator;
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.put()));
        if (FAILED(hr))
        {
            Fail(L"Could not create the WMI locator", hr);
            return false;
        }

        winrt::com_ptr<IWbemServices> services;
        hr = locator->ConnectServer(Bstr(LR"(ROOT\WMI)"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                    services.put());
        if (FAILED(hr))
        {
            Fail(L"Could not connect to ROOT\\WMI", hr);
            return false;
        }

        // The process never calls CoInitializeSecurity, so the proxy gets
        // the identity WMI expects here instead.
        hr = CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                               RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(hr))
        {
            Fail(L"Could not set the WMI proxy blanket", hr);
            return false;
        }

        m_services = std::move(services);
        return true;
    }

    std::optional<std::vector<BacklightPanel>> Backlight::Query()
    {
        m_lastErrorMessage.clear();
        if (!Connect()) return std::nullopt;

        winrt::com_ptr<IEnumWbemClassObject> results;
        HRESULT hr = m_services->ExecQuery(Bstr(L"WQL"),
                                           Bstr(L"SELECT InstanceName, Active, CurrentBrightness, Level "
                                                L"FROM WmiMonitorBrightness"),
                                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                           results.put());
        if (FAILED(hr))
        {
            Fail(L"Could not query WmiMonitorBrightness", hr);

            // A connection that went bad stays bad; start over next time.
            Close();
            return std::nullopt;
        }

        std::vector<BacklightPanel> panels;
        for (;;)
        {
            winrt::com_ptr<IWbemClassObject> row;
            ULONG returned = 0;
            hr = results->Next(QueryTimeoutMs, 1, row.put(), &returned);

            // No built-in panel is the common case, and on a desktop it
            // shows up here as "not supported" rather than as no rows.
            if (hr == WBEM_E_NOT_SUPPORTED) return panels;
            if (FAILED(hr))
            {
                Fail(L"Could not read WmiMonitorBrightness", hr);
                return std::nullopt;
            }
            if (returned == 0) break;

            Variant name, active, current, levels;
            if (FAILED(row->Get(L"InstanceName", 0, &name, nullptr, nullptr)) || name.vt != VT_BSTR ||
                FAILED(row->Get(L"CurrentBrightness", 0, &current, nullptr, nullptr)))
                continue;

            // An inactive instance is a panel Windows is not driving, such as
            // the laptop's own with its lid shut.
            if (SUCCEEDED(row->Get(L"Active", 0, &active, nullptr, nullptr)) && active.vt == VT_BOOL &&
                active.boolVal == VARIANT_FALSE)
                continue;

            auto percent = Integer(current);
            if (!percent) continue;

            BacklightPanel panel;
            panel.InstanceName = name.bstrVal;
            panel.Current = std::clamp(*percent, 0, 100);
            if (SUCCEEDED(row->Get(L"Level", 0, &levels, nullptr, nullptr)))
                panel.Levels = Bytes(levels);

            std::sort(panel.Levels.begin(), panel.Levels.end());
            panel.Levels.erase(std::unique(panel.Levels.begin(), panel.Levels.end()), panel.Levels.end());
            panels.push_back(std::move(panel));
        }

        return panels;
    }

    std::vector<BacklightPanel> Backlight::List()
    {
        return Query().value_or(std::vector<BacklightPanel>{});
    }

    std::optional<int> Backlight::Get(std::wstring const& instanceName)
    {
        auto panels = Query();
        if (!panels) return std::nullopt;

        for (auto const& p : *panels)
            if (p.InstanceName == instanceName)
                return p.Current;

        Fail(L"Panel not found", S_OK);
        return std::nullopt;
    }

    bool Backlight::Set(std::wstring const& instanceName, int percent)
    {
        m_lastErrorMessage.clear();
        if (!Connect()) return false;

        HRESULT hr = S_OK;
        if (!m_setParams)
        {
            winrt::com_ptr<IWbemClassObject> methods, definition;
            hr = m_services->GetObject(Bstr(L"WmiMonitorBrightnessMethods"), 0, nullptr, methods.put(), nullptr);
            if (SUCCEEDED(hr))
                hr = methods->GetMethod(L"WmiSetBrightness", 0, definition.put(), nullptr);
            if (SUCCEEDED(hr))
                hr = definition->SpawnInstance(0, m_setParams.put());
            if (FAILED(hr))
            {
                m_setParams = nullptr;
                Fail(L"Could not prepare WmiSetBrightness", hr);
                return false;
            }
        }

        Variant timeout, brightness;
        timeout.vt = VT_I4;
        timeout.lVal = SetTimeoutSeconds;
        brightness.vt = VT_UI1;
        brightness.bVal = static_cast<BYTE>(std::clamp(percent, 0, 100));

        hr = m_setParams->Put(L"Timeout", 0, &timeout, 0);
        if (SUCCEEDED(hr))
            hr = m_setParams->Put(L"Brightness", 0, &brightness, 0);
        if (FAILED(hr))
        {
            Fail(L"Could not fill in WmiSetBrightness", hr);
            return false;
        }

        std::wstring path = L"WmiMonitorBrightnessMethods.InstanceName=" + Quote(instanceName);
        hr = m_services->ExecMethod(Bstr(path.c_str()), Bstr(L"WmiSetBrightness"), 0, nullptr, m_setParams.get(),
                                    nullptr, nullptr);
        if (FAILED(hr))
        {
            Fail(L"Could not set the backlight", hr);
            return false;
        }

        return true;
    }

    void Backlight::Fail(std::wstring const& what, HRESULT hr)
    {
        m_lastErrorMessage = hr == S_OK ? what : what + L" (" + Hex(hr) + L")";
    }

    void Backlight::Close()
    {
        m_setParams = nullptr;
        m_services = nullptr;
    }
}
