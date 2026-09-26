#include "pch.h"
#include "SettingsWindow.xaml.h"
#include "AppIcon.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif
#include "Controls/SettingsCard.h"
#include "LocationDetector.h"
#include "Native/Shell.h"
#include "Services.h"
#include "Strings.h"
#include "Trace.h"
#include "WorldMap.xaml.h"

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace Core = ::AstroDimmer::Core;
using ::AstroDimmer::Services;
namespace Strings = ::AstroDimmer::Strings;
using ::AstroDimmer::Displays::BrightnessOrigin;
using ::AstroDimmer::Displays::DisplayItem;

namespace winrt::AstroDimmer::implementation
{
    namespace
    {
        Style LookupStyle(wchar_t const* key)
        {
            return Application::Current().Resources().Lookup(box_value(key)).as<Style>();
        }

        /// "Sunrise + 30 min": how the time above it was reached.
        std::wstring Caption(wchar_t const* labelKey, int offset)
        {
            auto label = Strings::Get(labelKey);
            if (offset == 0) return label;
            return Strings::Format(offset > 0 ? L"OffsetLater" : L"OffsetEarlier",
                                   { label, std::to_wstring(std::abs(offset)) });
        }

        /// A labelled slider row for a display card: label, glyph, slider,
        /// reading. The label column is fixed, so both rows' sliders start at
        /// the same x rather than stepping in and out with the label.
        struct LevelRow
        {
            Grid Root{ nullptr };
            Slider Level{ nullptr };
            TextBlock Label{ nullptr };
            TextBlock Reading{ nullptr };
        };

        LevelRow MakeLevelRow(hstring const& label, hstring const& glyph, int value)
        {
            LevelRow row;
            row.Root = Grid();

            ColumnDefinition labelColumn;
            labelColumn.Width(GridLength{ 180, GridUnitType::Pixel });
            ColumnDefinition glyphColumn;
            // Fixed, not Auto: rows without a glyph ("Current ...") keep the
            // gap so their slider lines up with the day and night sliders.
            glyphColumn.Width(GridLength{ 28, GridUnitType::Pixel });
            ColumnDefinition sliderColumn;
            ColumnDefinition readingColumn;
            readingColumn.Width(GridLengthHelper::Auto());
            for (auto const& c : { labelColumn, glyphColumn, sliderColumn, readingColumn })
                row.Root.ColumnDefinitions().Append(c);

            row.Label = TextBlock();
            row.Label.Text(label);
            row.Label.VerticalAlignment(VerticalAlignment::Center);
            row.Root.Children().Append(row.Label);

            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(16);
            icon.HorizontalAlignment(HorizontalAlignment::Left);
            icon.Style(LookupStyle(L"SecondaryIcon"));
            Grid::SetColumn(icon, 1);
            row.Root.Children().Append(icon);

            row.Level = Controls::Slider();
            row.Level.Minimum(0);
            row.Level.Maximum(100);
            row.Level.Value(value);
            row.Level.VerticalAlignment(VerticalAlignment::Center);
            Automation::AutomationProperties::SetName(row.Level, label);
            Grid::SetColumn(row.Level, 2);
            row.Root.Children().Append(row.Level);

            row.Reading = TextBlock();
            row.Reading.MinWidth(40);
            row.Reading.Margin(ThicknessHelper::FromLengths(12, 0, 0, 0));
            row.Reading.TextAlignment(TextAlignment::Right);
            row.Reading.VerticalAlignment(VerticalAlignment::Center);
            row.Reading.Style(LookupStyle(L"SecondaryText"));
            Documents::Typography::SetNumeralAlignment(row.Reading, FontNumeralAlignment::Tabular);
            row.Reading.Text(to_hstring(value) + L"%");
            Grid::SetColumn(row.Reading, 3);
            row.Root.Children().Append(row.Reading);

            return row;
        }

        /// Inert AND visibly so: a TextBlock does not grey out on its own.
        void SetRowEnabled(LevelRow const& row, bool enabled)
        {
            row.Level.IsEnabled(enabled);
            row.Label.Style(enabled ? nullptr : LookupStyle(L"DisabledText"));
            row.Reading.Style(LookupStyle(enabled ? L"SecondaryText" : L"DisabledText"));
        }

        /// Sets a row to a level read from the display.
        void ShowLevel(LevelRow const& row, int value)
        {
            row.Level.Value(value);
            row.Reading.Text(to_hstring(value) + L"%");
        }

        /// The line that sets the live row apart from the scheduled ones.
        Border MakeDivider()
        {
            Border line;
            line.Style(LookupStyle(L"CardDivider"));
            return line;
        }

        /// The On/Off word Windows Settings puts to the left of a switch.
        Grid MakeToggle(ToggleSwitch& toggle, TextBlock& state, hstring const& name, bool on)
        {
            Grid grid;
            ColumnDefinition text;
            text.Width(GridLengthHelper::Auto());
            ColumnDefinition swatch;
            swatch.Width(GridLengthHelper::Auto());
            grid.ColumnDefinitions().Append(text);
            grid.ColumnDefinitions().Append(swatch);
            grid.ColumnSpacing(12);

            state = TextBlock();
            state.VerticalAlignment(VerticalAlignment::Center);
            state.Text(on ? Strings::Get(L"On") : Strings::Get(L"Off"));
            grid.Children().Append(state);

            toggle = ToggleSwitch();
            toggle.OnContent(box_value(L""));
            toggle.OffContent(box_value(L""));
            toggle.MinWidth(0);
            toggle.IsOn(on);
            Automation::AutomationProperties::SetName(toggle, name);
            Grid::SetColumn(toggle, 1);
            grid.Children().Append(toggle);

            return grid;
        }
    }

    SettingsWindow::SettingsWindow()
    {
        InitializeComponent();

        // The title lives in the title bar, as in the Settings app. The
        // caption buttons stay Windows' own, snap layouts and all.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().TitleBar().PreferredHeightOption(TitleBarHeightOption::Tall);

        // The caption buttons are drawn by Windows for the SYSTEM theme; when
        // the window's own theme differs they vanish into the background, so
        // they are recoloured from the window's theme whenever it changes.
        auto paintCaption = [this]
        {
            bool dark = Root().ActualTheme() == ElementTheme::Dark;
            auto titleBar = AppWindow().TitleBar();
            Windows::UI::Color text = dark ? Windows::UI::Color{ 255, 255, 255, 255 } : Windows::UI::Color{ 228, 0, 0, 0 };
            Windows::UI::Color dim = dark ? Windows::UI::Color{ 93, 255, 255, 255 } : Windows::UI::Color{ 92, 0, 0, 0 };
            Windows::UI::Color hover = dark ? Windows::UI::Color{ 15, 255, 255, 255 } : Windows::UI::Color{ 9, 0, 0, 0 };
            Windows::UI::Color press = dark ? Windows::UI::Color{ 10, 255, 255, 255 } : Windows::UI::Color{ 6, 0, 0, 0 };
            titleBar.ButtonForegroundColor(text);
            titleBar.ButtonHoverForegroundColor(text);
            titleBar.ButtonPressedForegroundColor(dim);
            titleBar.ButtonInactiveForegroundColor(dim);
            titleBar.ButtonBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonInactiveBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonHoverBackgroundColor(hover);
            titleBar.ButtonPressedBackgroundColor(press);
        };
        Root().ActualThemeChanged([paintCaption](auto&&, auto&&) { paintCaption(); });
        Root().Loaded([paintCaption](auto&&, auto&&) { paintCaption(); });

        // Sized in DIPs for the monitor it opens on, and centred there.
        auto hwnd = GetWindowFromWindowId(AppWindow().Id());
        double scale = GetDpiForWindow(hwnd) / 96.0;
        auto area = DisplayArea::GetFromWindowId(AppWindow().Id(), DisplayAreaFallback::Primary).WorkArea();
        int width = std::min(static_cast<int>(900 * scale), area.Width);
        int height = std::min(static_cast<int>(820 * scale), area.Height);
        AppWindow().MoveAndResize({ area.X + (area.Width - width) / 2, area.Y + (area.Height - height) / 2, width, height });

        if (auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
        {
            presenter.PreferredMinimumWidth(static_cast<int>(720 * scale));
            presenter.PreferredMinimumHeight(static_cast<int>(560 * scale));
        }

        // The app's own icon, for the taskbar button and Alt+Tab. Loaded
        // large and left for the shell to scale.
        m_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               64, 64, LR_DEFAULTCOLOR));
        AppWindow().SetIcon(GetIconIdFromIcon(m_icon));

        auto& services = Services::Get();

        m_windowsLocationAvailable = ::AstroDimmer::Location::WindowsLocationAvailable();

        if (auto version = ::AstroDimmer::Native::Shell::Version(); !version.empty())
            VersionText().Text(Strings::Format(L"Version", { version }));

        // ---- wiring

        RunAtLogin().Toggled([this](auto&&, auto&&)
        {
            if (m_loading) return;

            bool wanted = RunAtLogin().IsOn();

            // The registry write can fail under policy. Reflect what actually
            // happened rather than leaving the switch lying about the state.
            if (!::AstroDimmer::Native::Shell::SetStartupEnabled(wanted))
                SetToggle(RunAtLogin(), RunAtLoginState(), ::AstroDimmer::Native::Shell::StartupEnabled());
            else
                RunAtLoginState().Text(wanted ? Strings::Get(L"On") : Strings::Get(L"Off"));
        });

        AstroEnabled().Toggled([this](auto&&, auto&&)
        {
            AstroEnabledState().Text(AstroEnabled().IsOn() ? Strings::Get(L"On") : Strings::Get(L"Off"));
            OnFormChanged();
        });

        auto stepperChanged = [this](auto&&, auto&&) { OnFormChanged(); };
        DayOffset().ValueChanged(stepperChanged);
        NightOffset().ValueChanged(stepperChanged);
        FadeMinutes().ValueChanged(stepperChanged);

        LocationMap().LocationChanged([this](auto&&, auto&&) { OnFormChanged(); });
        DetectButton().Click([this](auto&&, auto&&) { DetectAsync(false); });

        QuitButton().Click([](auto&&, auto&&)
        {
            if (auto& quit = Services::Get().Quit)
                quit();
        });

        // Displays can arrive after the window opens - the first enumeration
        // takes a moment, and docking changes the set later - so the cards
        // follow the list rather than being a snapshot.
        m_displaysToken = services.Displays->DisplaysChanged.Add([this] { BuildDisplayCards(); });

        // The title-bar icon is the tray's: sunrise, day, sunset, night.
        auto showStage = [this](::AstroDimmer::AstroStage stage)
        {
            TitleIcon().Glyph(::AstroDimmer::StageGlyph(stage));
        };
        showStage(services.Stage);
        m_stageToken = services.StageChanged.Add(showStage);

        Closed([this](auto&&, auto&&)
        {
            ClearDisplayCards();
            auto& s = Services::Get();
            s.Displays->DisplaysChanged.Remove(m_displaysToken);
            s.StageChanged.Remove(m_stageToken);
            if (m_icon) DestroyIcon(m_icon);
            m_icon = nullptr;
        });

        SetToggle(RunAtLogin(), RunAtLoginState(), ::AstroDimmer::Native::Shell::StartupEnabled());

        Load();
        BuildDisplayCards();
        m_loading = false;
    }

    void SettingsWindow::RestoreAndActivate()
    {
        // Activating alone does nothing to a minimised window - it stays in
        // the taskbar and the click looks ignored. Restore brings back a
        // window that was maximised before it was minimised as maximised.
        if (auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
            if (presenter.State() == OverlappedPresenterState::Minimized)
                presenter.Restore();

        Activate();
    }

    void SettingsWindow::SetToggle(ToggleSwitch const& toggle, TextBlock const& state, bool on)
    {
        bool loading = m_loading;
        m_loading = true;
        toggle.IsOn(on);
        state.Text(on ? Strings::Get(L"On") : Strings::Get(L"Off"));
        m_loading = loading;
    }

    void SettingsWindow::Load()
    {
        auto const& a = Services::Get().Settings.Astro;

        SetToggle(AstroEnabled(), AstroEnabledState(), a.Enabled);

        // Exactly (0,0) is the convention for "not set", so the pin stays off
        // rather than claiming a spot in the Gulf of Guinea.
        bool located = !(a.Latitude == 0 && a.Longitude == 0);
        if (located)
            LocationMap().ShowLocation(a.Latitude, a.Longitude);
        else
            LocationMap().ClearLocation();

        // Nothing saved yet: ask Windows, so the pin starts where you are
        // rather than making you find yourself on a world map cold.
        if (!located)
            DetectAsync(true);

        // The effective length already folds the on/off flag in, which is
        // what a stepper whose zero means "off" needs.
        FadeMinutes().Value(std::clamp(a.EffectiveFadeMinutes(), 0, 180));
        DayOffset().Value(std::clamp(a.DayOffset, -180, 180));
        NightOffset().Value(std::clamp(a.NightOffset, -180, 180));

        // Present from the start, not only once something has happened.
        SetDetectStatus(L"");

        UpdateStatus();
        UpdateCoordinates();
    }

    void SettingsWindow::OnFormChanged()
    {
        // Windows 11 settings apply as you change them; there is no Save.
        if (m_loading) return;

        ReadForm();
        Services::Get().SaveAndNotify();

        UpdateStatus();
        UpdateCoordinates();
    }

    void SettingsWindow::ReadForm()
    {
        auto& a = Services::Get().Settings.Astro;

        a.Enabled = AstroEnabled().IsOn();

        // An unplaced pin stores (0,0), which is what the rest of the app
        // reads back as "not set".
        a.Latitude = LocationMap().HasLocation() ? LocationMap().Latitude() : 0;
        a.Longitude = LocationMap().HasLocation() ? LocationMap().Longitude() : 0;

        // The flag stays in the file - Glimmer reads it - but the stepper is
        // now its only source.
        a.FadeEnabled = FadeMinutes().Value() > 0;
        a.FadeMinutes = FadeMinutes().Value();
        a.DayOffset = DayOffset().Value();
        a.NightOffset = NightOffset().Value();
    }

    void SettingsWindow::UpdateCoordinates()
    {
        std::optional<double> lat, lon;
        if (LocationMap().HasLocation())
        {
            lat = LocationMap().Latitude();
            lon = LocationMap().Longitude();
        }
        CoordinatesText().Text(WorldMap::Describe(lat, lon));
    }

    void SettingsWindow::UpdateStatus()
    {
        // Redraws the strip for whatever the form now says, so the effect of
        // coordinates and offsets is visible without waiting for sunset.
        auto const& a = Services::Get().Settings.Astro;
        auto state = Core::AstroEngine::GetState(a.Latitude, a.Longitude, a.DayOffset, a.NightOffset,
                                                 a.EffectiveFadeMinutes(), Core::LocalNow());

        if (!state)
        {
            // No coordinates, or a polar day: the whole card goes rather than
            // leaving an empty trough, which would read as a schedule that
            // does nothing - not the same as one that cannot be worked out.
            Timeline().ClearSchedule();
            TimelineCard().Visibility(Visibility::Collapsed);
            UpdateSteppers(std::nullopt);
            return;
        }

        TimelineCard().Visibility(Visibility::Visible);
        Timeline().SetSchedule(state->Bounds.DayStart, state->Bounds.NightStart, a.EffectiveFadeMinutes());
        UpdateSteppers(state->Bounds);
    }

    void SettingsWindow::UpdateSteppers(std::optional<Core::AstroEngine::Boundaries> const& b)
    {
        // The bold line is the RESULT - the clock time a boundary lands on, or
        // the fade duration - and the caption says how it was reached.
        // Showing only the offset would leave you doing the arithmetic.
        DayOffset().ValueText(b ? Strings::Time(b->DayStart) : L"--:--");
        DayOffset().CaptionText(Caption(L"Sunrise", DayOffset().Value()));

        NightOffset().ValueText(b ? Strings::Time(b->NightStart) : L"--:--");
        NightOffset().CaptionText(Caption(L"Sunset", NightOffset().Value()));

        // Zero is a state, not a quantity, so it says what it does rather than
        // reading as "0 minutes to fade".
        int fade = FadeMinutes().Value();
        FadeMinutes().ValueText(fade == 0 ? hstring{ Strings::Get(L"Off") } : to_hstring(fade));
        FadeMinutes().CaptionText(Strings::Get(fade == 0 ? L"FadeAtOnce" : L"FadeMinutes"));
    }

    void SettingsWindow::SetDetectStatus(std::wstring const& text)
    {
        // Falls back to the standing hint where it is true, so the line is
        // not a thing that appears and vanishes - the reason this message
        // kept being missed.
        //
        // The standing hint is shown when Windows cannot supply a position,
        // because then the button can only reach the website, and that is
        // worth knowing before pressing it.
        std::wstring shown = !text.empty() ? text : m_windowsLocationAvailable ? L"" : Strings::Get(L"DetectHint");
        DetectStatus().Text(shown);

        // Collapsed, not just blank: an empty line still takes its height.
        DetectStatus().Visibility(shown.empty() ? Visibility::Collapsed : Visibility::Visible);
    }

    fire_and_forget SettingsWindow::DetectAsync(bool automatic)
    {
        // Also runs once when the window opens with no location saved. That
        // automatic attempt stays SILENT about failure: nobody asked for it,
        // so a refusal is not an error worth a message.
        if (m_detecting) co_return;
        m_detecting = true;

        auto strong = get_strong();
        DetectButton().IsEnabled(false);

        // No "Locating…" yet. Where Windows answers - most machines, in a few
        // hundred milliseconds - it would appear and vanish before it could be
        // read, which looks like a glitch. The disabled button carries the
        // meantime until the pause is long enough to be worth explaining.
        auto slow = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        slow.Interval(std::chrono::milliseconds(600));
        slow.IsRepeating(false);
        slow.Tick([this](auto&&, auto&&) { if (m_detecting) SetDetectStatus(Strings::Get(L"Locating")); });
        slow.Start();

        using namespace ::AstroDimmer::Location;
        auto result = std::make_shared<Result>();
        co_await FromWindowsAsync(std::chrono::seconds(10), result);

        // Windows would not say. Pressing the button is the permission to try
        // the other way - but ONLY a press: the automatic attempt stops at the
        // refusal and puts nothing on the wire.
        if (!result->Found() && !automatic)
        {
            SetDetectStatus(Strings::Format(L"AskingWebsiteAfter", { WhyWindowsFailed(*result) }));
            result = std::make_shared<Result>();
            co_await FromIpAddressAsync(std::chrono::seconds(10), result);
        }

        slow.Stop();
        DetectButton().IsEnabled(true);
        m_detecting = false;

        // Detection takes up to twenty seconds and the map is live the whole
        // time. If you placed the pin yourself meanwhile, your choice stands.
        if (automatic && LocationMap().HasLocation())
            co_return;

        if (result->Found())
        {
            // Through the map, so it rounds, clamps and announces exactly as a
            // drag would - a detected position is not a special case.
            LocationMap().SetLocation(result->Latitude, result->Longitude);

            // Say where the answer came from when it was a guess: an IP
            // estimate can be a city off, or a country off behind a VPN.
            SetDetectStatus(result->Origin == Source::IpAddress
                                ? Strings::Get(L"RoughGuess")
                                : std::wstring());
        }
        else
        {
            SetDetectStatus(automatic ? L"" : Explain(*result));
        }
    }

    void SettingsWindow::ClearDisplayCards()
    {
        for (auto const& [item, token] : m_itemTokens)
            item->Changed.Remove(token);
        m_itemTokens.clear();

        ScheduleDisplays().Children().Clear();
        ContrastDisplays().Children().Clear();
    }

    void SettingsWindow::BuildDisplayCards()
    {
        auto& services = Services::Get();

        ClearDisplayCards();

        auto const& displays = services.Displays->Displays();

        NoScheduleDisplays().Visibility(displays.empty() ? Visibility::Visible : Visibility::Collapsed);

        // Only displays with a contrast control get a card below, so the
        // section can be empty with displays attached - a laptop's own panel
        // never has one - and says which kind of empty it is.
        bool anyContrast = std::any_of(displays.begin(), displays.end(), [](auto const& d) { return d->SupportsContrast; });
        NoContrastDisplays().Text(Strings::Get(displays.empty() ? L"NoContrastDisplays/Text" : L"NoContrastSupport"));
        NoContrastDisplays().Visibility(anyContrast ? Visibility::Collapsed : Visibility::Visible);

        for (auto const& display : displays)
        {
            std::wstring key = display->DeviceKey;
            auto levels = services.Settings.Astro.LevelsFor(key);

            // A display normally has its own entry from the moment it was
            // first seen. One carried over from an older settings file gets
            // its entry here, on the first slider move; until then it follows
            // the schedule's defaults.
            auto persist = [key](auto update)
            {
                auto& s = Services::Get();
                auto current = s.Settings.Astro.LevelsFor(key);
                update(current);
                s.Settings.Astro.PerDisplay[key] = current;
                s.SaveAndNotify();
            };

            // ---- brightness card

            AstroDimmer::SettingsCard brightness;
            brightness.Glyph(L"");
            brightness.Header(display->Name);
            brightness.Description(display->Description);

            // One switch for the whole monitor: a display taken out of the
            // flyout is one AstroDimmer should not touch, and the schedule
            // already skips hidden displays. Its levels stay on screen,
            // disabled, so what it would return to is still visible.
            bool shown = !services.Settings.HiddenDisplays.contains(key);
            ToggleSwitch shownToggle{ nullptr };
            TextBlock shownState{ nullptr };
            brightness.Content(MakeToggle(shownToggle, shownState, hstring{ Strings::Get(L"ShowInPanel") }, shown));

            // What the panel is at right now, on its own row above the two
            // it is scheduled to reach. It is the flyout's slider: moving it
            // sets the display and holds off the schedule, as a drag there
            // does, and it follows the display when anything else moves it.
            std::weak_ptr<DisplayItem> weak = display;
            auto updating = std::make_shared<bool>(false);

            StackPanel levelsPanel;
            levelsPanel.Spacing(12);
            auto nowBrightness = MakeLevelRow(hstring{ Strings::Get(L"CurrentBrightness") }, L"", display->Brightness());
            levelsPanel.Children().Append(nowBrightness.Root);
            levelsPanel.Children().Append(MakeDivider());
            SetRowEnabled(nowBrightness, shown);
            auto day = MakeLevelRow(hstring{ Strings::Get(L"DaytimeBrightness") }, L"", levels.Day);
            auto night = MakeLevelRow(hstring{ Strings::Get(L"NightBrightness") }, L"", levels.Night);
            levelsPanel.Children().Append(day.Root);
            levelsPanel.Children().Append(night.Root);
            SetRowEnabled(day, shown);
            SetRowEnabled(night, shown);
            brightness.Body(levelsPanel);

            nowBrightness.Level.ValueChanged([weak, updating, nowBrightness](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
            {
                int v = static_cast<int>(std::lround(e.NewValue()));
                nowBrightness.Reading.Text(to_hstring(v) + L"%");
                if (*updating) return;
                if (auto d = weak.lock())
                    d->SetBrightness(v, BrightnessOrigin::User);
            });

            shownToggle.Toggled([key, shownToggle, shownState, nowBrightness, day, night](auto&&, auto&&)
            {
                bool on = shownToggle.IsOn();
                shownState.Text(on ? Strings::Get(L"On") : Strings::Get(L"Off"));
                SetRowEnabled(nowBrightness, on);
                SetRowEnabled(day, on);
                SetRowEnabled(night, on);

                // Stored as the exception - a hidden display is listed, a
                // shown one is not - so an untouched monitor needs no entry.
                auto& s = Services::Get();
                if (on) s.Settings.HiddenDisplays.erase(key);
                else s.Settings.HiddenDisplays.insert(key);
                s.SaveAndNotify();
            });

            day.Level.ValueChanged([persist, day](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
            {
                int v = static_cast<int>(std::lround(e.NewValue()));
                day.Reading.Text(to_hstring(v) + L"%");
                persist([v](Core::DisplayLevels& l) { l.Day = v; });
            });

            night.Level.ValueChanged([persist, night](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
            {
                int v = static_cast<int>(std::lround(e.NewValue()));
                night.Reading.Text(to_hstring(v) + L"%");
                persist([v](Core::DisplayLevels& l) { l.Night = v; });
            });

            ScheduleDisplays().Children().Append(brightness);

            // ---- contrast card

            // Stays empty for a display without contrast: nothing to follow.
            LevelRow nowContrast;

            // Only for displays that answered VCP 0x12. One that did not has
            // no contrast to set, and a card saying so is just noise.
            if (display->SupportsContrast)
            {
                AstroDimmer::SettingsCard contrast;
                contrast.Glyph(L"");
                contrast.Header(display->Name);
                contrast.Description(display->Description);

                // Off by default, so this moves nobody's carefully set panel
                // until they ask.
                ToggleSwitch contrastToggle{ nullptr };
                TextBlock contrastState{ nullptr };
                contrast.Content(MakeToggle(contrastToggle, contrastState, hstring{ Strings::Get(L"AdjustContrast") },
                                            levels.Contrast));

                StackPanel contrastPanel;
                contrastPanel.Spacing(12);
                nowContrast = MakeLevelRow(hstring{ Strings::Get(L"CurrentContrast") }, L"", display->Contrast());
                contrastPanel.Children().Append(nowContrast.Root);
                contrastPanel.Children().Append(MakeDivider());
                SetRowEnabled(nowContrast, levels.Contrast);
                auto dayContrast = MakeLevelRow(hstring{ Strings::Get(L"DaytimeContrast") }, L"", levels.DayContrast);
                auto nightContrast = MakeLevelRow(hstring{ Strings::Get(L"NightContrast") }, L"", levels.NightContrast);
                contrastPanel.Children().Append(dayContrast.Root);
                contrastPanel.Children().Append(nightContrast.Root);
                SetRowEnabled(dayContrast, levels.Contrast);
                SetRowEnabled(nightContrast, levels.Contrast);
                contrast.Body(contrastPanel);

                nowContrast.Level.ValueChanged([weak, updating, nowContrast](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
                {
                    int v = static_cast<int>(std::lround(e.NewValue()));
                    nowContrast.Reading.Text(to_hstring(v) + L"%");
                    if (*updating) return;
                    if (auto d = weak.lock())
                        d->SetContrast(v);
                });

                contrastToggle.Toggled([persist, contrastToggle, contrastState, nowContrast, dayContrast, nightContrast](auto&&, auto&&)
                {
                    bool on = contrastToggle.IsOn();
                    contrastState.Text(on ? Strings::Get(L"On") : Strings::Get(L"Off"));
                    SetRowEnabled(nowContrast, on);
                    SetRowEnabled(dayContrast, on);
                    SetRowEnabled(nightContrast, on);
                    persist([on](Core::DisplayLevels& l) { l.Contrast = on; });
                });

                dayContrast.Level.ValueChanged([persist, dayContrast](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
                {
                    int v = static_cast<int>(std::lround(e.NewValue()));
                    dayContrast.Reading.Text(to_hstring(v) + L"%");
                    persist([v](Core::DisplayLevels& l) { l.DayContrast = v; });
                });

                nightContrast.Level.ValueChanged([persist, nightContrast](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
                {
                    int v = static_cast<int>(std::lround(e.NewValue()));
                    nightContrast.Reading.Text(to_hstring(v) + L"%");
                    persist([v](Core::DisplayLevels& l) { l.NightContrast = v; });
                });

                ContrastDisplays().Children().Append(contrast);
            }

            // Values set from here are the display's own, not the user's, so
            // the sliders' handlers must not send them back out.
            int token = display->Changed.Add([weak, updating, nowBrightness, nowContrast]
            {
                auto d = weak.lock();
                if (!d) return;
                *updating = true;
                ShowLevel(nowBrightness, d->Brightness());
                if (nowContrast.Level)
                    ShowLevel(nowContrast, d->Contrast());
                *updating = false;
            });
            m_itemTokens.emplace_back(display, token);
        }
    }
}
