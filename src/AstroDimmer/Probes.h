#pragma once

#include <functional>

namespace winrt::AstroDimmer::implementation
{
    struct FlyoutWindow;
}

/// Measurement and hardware-verification aids, each switched on from the
/// command line and each writing a text file next to the exe, so a run can
/// be diagnosed without a debugger attached.
namespace AstroDimmer::Probes
{
    /// --ddc / --ddc-write: drives the real DDC/CI layer against the attached
    /// displays and writes what it finds to ddc-probe.txt - raw results,
    /// failures included, since those are the interesting cases. With writes,
    /// nudges each monitor's brightness and restores it. Runs on its own
    /// thread (one thread, as every DDC conversation must) and calls done on
    /// that thread when finished.
    void RunDdcProbe(bool includeWrites, std::function<void()> done);
    /// --cycle [--destroy]: shows and hides the panel and samples memory at
    /// each step into memory-cycle.txt, then calls done. A tray flyout is
    /// hidden almost all the time, so what matters is whether showing it once
    /// leaves the process heavier afterwards.
    void RunMemoryCycle(winrt::com_ptr<winrt::AstroDimmer::implementation::FlyoutWindow> const& flyout,
                        bool destroyOnHide, std::function<void()> done);

    /// diagnostics.txt: the facts that explain a run - arguments, OS build,
    /// backdrop support, transparency, and memory once things have settled.
    void WriteDiagnostics();
}
