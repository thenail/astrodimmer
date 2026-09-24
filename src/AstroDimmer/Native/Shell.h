#pragma once

#include <string>

/// Small reads and writes against the Windows shell's own settings.
namespace AstroDimmer::Native::Shell
{
    /// The taskbar, Start and the tray follow "Windows mode"; application
    /// windows follow "App mode". They are separate settings, and picking the
    /// wrong one puts a white glyph on a light taskbar, where it vanishes.
    /// Absent or unreadable counts as light, Windows' own default.
    bool TaskbarIsDark();
    bool AppsAreDark();

    /// This executable's full path.
    std::wstring ExecutablePath();

    /// Run-at-login, via the per-user Run key: no elevation, the place users
    /// expect to find it in Task Manager's Startup tab, and removable there.
    ///
    /// True only when the entry exists AND points at this executable - a
    /// stale entry from another install location would not launch this build.
    bool StartupEnabled();

    /// Returns false if the registry could not be written.
    bool SetStartupEnabled(bool enabled);
}
