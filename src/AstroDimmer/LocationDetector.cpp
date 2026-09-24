#include "pch.h"
#include "LocationDetector.h"
#include "CommandLine.h"
#include "Json.h"
#include "Strings.h"
#include "Trace.h"

#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

using namespace winrt::Windows::Devices::Geolocation;
using namespace winrt::Windows::Web::Http;

namespace AstroDimmer::Location
{
    namespace
    {
        /// Coarse on purpose. Sunrise moves about four minutes per degree of
        /// longitude, so a few kilometres is far below what changes any answer
        /// here - and asking for coarse avoids spinning up GPS hardware.
        constexpr uint32_t AccuracyMetres = 5000;

        /// A neutral, documented provider rather than someone's personal
        /// server, which can vanish without notice.
        constexpr wchar_t IpLookupUrl[] = L"https://ipapi.co/json/";

        /// Cancels an operation that has not finished in time. The returned
        /// timer is stopped by the caller once the operation completes.
        template <typename Operation>
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer CancelAfter(Operation const& operation,
                                                                             std::chrono::milliseconds timeout)
        {
            auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            timer.Interval(timeout);
            timer.IsRepeating(false);
            timer.Tick([operation](auto&& sender, auto&&)
            {
                sender.Stop();
                operation.Cancel();
            });
            timer.Start();
            return timer;
        }

        std::wstring Fixed4(double value)
        {
            wchar_t buffer[32];
            swprintf_s(buffer, L"%.4f", value);
            return buffer;
        }
    }

    winrt::Windows::Foundation::IAsyncAction FromWindowsAsync(std::chrono::milliseconds timeout,
                                                              std::shared_ptr<Result> result)
    {
        result->Origin = Source::Windows;

        // --no-os-location: pretend Windows refused. The path behind that
        // refusal is otherwise reachable only by turning off a system privacy
        // setting, which is a lot to ask of anyone testing its wording.
        if (CommandLine::Has(L"--no-os-location"))
        {
            Trace::Log(L"location: refused by --no-os-location");
            result->Status = Outcome::Denied;
            co_return;
        }

        try
        {
            auto access = co_await Geolocator::RequestAccessAsync();
            if (access != GeolocationAccessStatus::Allowed)
            {
                Trace::Log(L"location: access " + std::to_wstring(static_cast<int>(access)));
                result->Status = Outcome::Denied;
                co_return;
            }

            Geolocator locator;
            locator.DesiredAccuracy(PositionAccuracy::Default);
            locator.DesiredAccuracyInMeters(winrt::Windows::Foundation::IReference<uint32_t>(AccuracyMetres));

            auto operation = locator.GetGeopositionAsync();
            auto timer = CancelAfter(operation, timeout);
            auto position = co_await operation;
            timer.Stop();

            auto point = position.Coordinate().Point().Position();
            Trace::Log(L"location: windows found " + Fixed4(point.Latitude) + L"," + Fixed4(point.Longitude));

            result->Status = Outcome::Found;
            result->Latitude = point.Latitude;
            result->Longitude = point.Longitude;
        }
        catch (winrt::hresult_canceled const&)
        {
            Trace::Log(L"location: windows timed out");
            result->Status = Outcome::TimedOut;
        }
        catch (winrt::hresult_error const& ex)
        {
            // Thrown for a machine with no location stack at all, and the
            // settings window must not die with it.
            Trace::Log(L"location: unavailable (" + std::wstring(ex.message()) + L")");
            result->Status = Outcome::Unavailable;
        }
    }

    winrt::Windows::Foundation::IAsyncAction FromIpAddressAsync(std::chrono::milliseconds timeout,
                                                                std::shared_ptr<Result> result)
    {
        result->Origin = Source::IpAddress;
        result->Status = Outcome::Unavailable;

        try
        {
            HttpClient client;
            client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"AstroDimmer");

            auto operation = client.GetAsync(winrt::Windows::Foundation::Uri(IpLookupUrl));
            auto timer = CancelAfter(operation, timeout);
            auto response = co_await operation;

            if (!response.IsSuccessStatusCode())
            {
                timer.Stop();
                Trace::Log(L"location: ip lookup HTTP " + std::to_wstring(static_cast<int>(response.StatusCode())));
                co_return;
            }

            auto body = co_await response.Content().ReadAsStringAsync();
            timer.Stop();

            auto json = Core::Json::Parse(std::wstring_view(body));
            if (!json)
            {
                Trace::Log(L"location: ip lookup gave no JSON");
                co_return;
            }

            // The provider reports rate limits as a 200 with an error body.
            if (auto error = json->Find(L"error"); error && error->AsBool().value_or(false))
            {
                auto reason = json->Find(L"reason");
                Trace::Log(L"location: ip lookup refused - " + (reason ? reason->AsString().value_or(L"") : L""));
                co_return;
            }

            auto lat = json->Find(L"latitude");
            auto lon = json->Find(L"longitude");
            if (!lat || !lon || !lat->AsNumber() || !lon->AsNumber())
            {
                Trace::Log(L"location: ip lookup gave no coordinates");
                co_return;
            }

            // The city, not the address, and only to the log.
            auto city = json->Find(L"city");
            Trace::Log(L"location: ip lookup found " + Fixed4(*lat->AsNumber()) + L"," + Fixed4(*lon->AsNumber()) +
                       L" (" + (city ? city->AsString().value_or(L"") : L"") + L")");

            result->Status = Outcome::Found;
            result->Latitude = *lat->AsNumber();
            result->Longitude = *lon->AsNumber();
        }
        catch (winrt::hresult_canceled const&)
        {
            Trace::Log(L"location: ip lookup timed out");
            result->Status = Outcome::TimedOut;
        }
        catch (winrt::hresult_error const& ex)
        {
            Trace::Log(L"location: ip lookup failed (" + std::wstring(ex.message()) + L")");
            result->Status = Outcome::Unavailable;
        }
    }

    bool WindowsLocationAvailable()
    {
        if (CommandLine::Has(L"--no-os-location"))
            return false;

        // The switches the access check consults, in two hives: the service
        // itself is device-wide, while the user's own access and the
        // separate "let desktop apps access your location" toggle an
        // unpackaged app falls under are per-user. Reading only the machine
        // hive looks right on a machine where location works, and is wrong
        // on exactly the machines this is for.
        constexpr wchar_t Consent[] =
            LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\location)";

        auto allowed = [](HKEY hive, std::wstring const& path)
        {
            wchar_t value[64]{};
            DWORD size = sizeof(value);
            if (RegGetValueW(hive, path.c_str(), L"Value", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
                return true;
            return _wcsicmp(value, L"Deny") != 0;
        };

        return allowed(HKEY_LOCAL_MACHINE, Consent) && allowed(HKEY_CURRENT_USER, Consent) &&
               allowed(HKEY_CURRENT_USER, std::wstring(Consent) + L"\\NonPackaged");
    }

    std::wstring WhyWindowsFailed(Result const& result)
    {
        switch (result.Status)
        {
        case Outcome::Denied: return Strings::Get(L"WindowsDenied");
        case Outcome::TimedOut: return Strings::Get(L"WindowsTimedOut");
        default: return Strings::Get(L"WindowsUnavailable");
        }
    }

    std::wstring Explain(Result const& result)
    {
        // Having already fallen back to the network and still failed, the
        // Windows permission is no longer the useful thing to talk about.
        if (result.Origin == Source::IpAddress)
        {
            return Strings::Get(result.Status == Outcome::TimedOut ? L"ExplainWebsiteNoAnswer" : L"ExplainWebsiteFailed");
        }

        switch (result.Status)
        {
        case Outcome::Denied:
            return Strings::Get(L"ExplainDenied");
        case Outcome::TimedOut:
            return Strings::Get(L"ExplainTimedOut");
        default:
            return Strings::Get(L"ExplainUnavailable");
        }
    }
}
