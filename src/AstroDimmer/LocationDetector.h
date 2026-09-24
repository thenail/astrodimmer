#pragma once

#include <memory>
#include <string>

/// Works out where this machine is, so the location pin can start somewhere
/// useful instead of in the Gulf of Guinea.
///
/// Two sources, deliberately in this order:
///
///   1. FromWindowsAsync - the OS location service. Nothing leaves
///      AstroDimmer; Windows may consult the network itself, but that is the
///      user's existing relationship with the OS, governed by a permission
///      they can see and revoke.
///
///   2. FromIpAddressAsync - an HTTPS request whose SERVER infers position
///      from the source address. That sends the user's IP to a third party,
///      so it never runs on its own initiative: only when someone presses the
///      detect button, and only after Windows has declined.
namespace AstroDimmer::Location
{
    enum class Outcome
    {
        Found,

        /// Location is off, or desktop apps are not allowed it.
        Denied,

        /// No fix in time - indoors with no wifi positioning, typically.
        TimedOut,

        /// No location hardware or service on this machine.
        Unavailable,
    };

    enum class Source
    {
        None,

        /// The OS location service.
        Windows,

        /// A server's guess from our IP address. City-level at best.
        IpAddress,
    };

    struct Result
    {
        Outcome Status{ Outcome::Unavailable };
        Source Origin{ Source::None };
        double Latitude{ 0 };
        double Longitude{ 0 };

        bool Found() const { return Status == Outcome::Found; }
    };

    /// Completes on the calling (UI) thread. The first call raises Windows'
    /// consent prompt, which is why it must start on the UI thread.
    winrt::Windows::Foundation::IAsyncAction FromWindowsAsync(std::chrono::milliseconds timeout,
                                                              std::shared_ptr<Result> result);

    winrt::Windows::Foundation::IAsyncAction FromIpAddressAsync(std::chrono::milliseconds timeout,
                                                                std::shared_ptr<Result> result);

    /// Whether Windows is in a position to answer at all, judged WITHOUT
    /// asking it - asking raises the consent prompt the first time, which is
    /// far too much for merely opening Settings. A missing or unreadable value
    /// means "assume it works".
    bool WindowsLocationAvailable();

    /// Why Windows could not answer, for the line that then offers another way.
    std::wstring WhyWindowsFailed(Result const& result);

    /// What to tell the user when nothing produced a position.
    std::wstring Explain(Result const& result);
}
