<div align="center">

<img src="docs/img/icon.png" width="72" alt="">

# Astro Dimmer

**Your screens, in step with the sun.**

Astro Dimmer dims your monitors at sunset and brightens them again at sunrise, automatically, every day.

[**Download for Windows**](https://github.com/thenail/astrodimmer/releases/latest) · [Website](https://thenail.github.io/astrodimmer/) · [Questions](https://thenail.github.io/astrodimmer/#faq)

<img src="docs/img/flyout-night.png" width="460" alt="The Astro Dimmer panel above the taskbar at night, with a slider for each of four monitors">

</div>

## What it does

- **Follows the sun.** Put a pin on the map and Astro Dimmer works out sunrise and sunset for every day of the year, wherever you are.
- **One slider per monitor.** Click the icon on the taskbar for a panel that looks like part of Windows.
- **A level for each screen.** Set a daytime and a night-time level per monitor. Contrast can follow the schedule too.
- **Your timing.** Shift the change earlier or later by up to three hours, and fade gently instead of switching at once.
- **Steps aside when you want it to.** Drag a slider yourself and that screen is left alone until the next sunrise or sunset.
- **Quiet and private.** It lives on the taskbar, starts with Windows and needs no account. Your location stays on your computer.
- **19 languages**, following your Windows language.

<p align="center">
  <img src="docs/img/settings-displays.png" width="720" alt="Settings: daytime and night-time brightness for two monitors">
</p>

## Install

Download the installer from the [latest release](https://github.com/thenail/astrodimmer/releases/latest):

| Your PC | File |
| --- | --- |
| Most PCs (Intel and AMD) | `AstroDimmer-setup-<version>.exe` |
| ARM-based PCs (e.g. Snapdragon) | `AstroDimmer-setup-ARM64-<version>.exe` |

It installs for your user account, with no administrator password needed. Windows 11, and Windows 10 from version 1809 (October 2018 Update) onwards.

## Will it work with my monitor?

Astro Dimmer talks to external monitors over **DDC/CI**, the standard most monitors from recent years support. If a monitor doesn't appear, look for a DDC/CI setting in the monitor's own on-screen menu and switch it on. The built-in screen of a laptop isn't supported.

## Building from source

Needs Visual Studio 2026 with the *Desktop development with C++* workload and the *Windows App SDK C++* component.

```powershell
.\build.ps1                               # Debug x64, runs the tests
.\build.ps1 -Configuration Release -Platform ARM64
.\build.ps1 -Installer                    # Release build plus the setup exe
```

The app is C++/WinRT on WinUI 3. `src/AstroDimmer.Core` holds the platform-independent parts (solar times, scheduling, settings), covered by `tests/AstroDimmer.Tests`.

Handy switches when working on the UI: `--show` opens the panel at startup, `--pin` keeps it open, `--settings` opens Settings, `--theme=dark|light`, `--lang=de`, and `--simulate-displays=3` adds extra monitors to the UI.

## License

[MIT](LICENSE)
