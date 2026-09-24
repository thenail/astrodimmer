#pragma once

#include "FlyoutWindow.g.h"
#include "Displays/DisplayItem.h"

namespace winrt::AstroDimmer::implementation
{
    struct FlyoutWindow : FlyoutWindowT<FlyoutWindow>
    {
        FlyoutWindow();

        /// Opens the panel, or closes it if it is open. A click that has just
        /// dismissed the panel (by taking focus away) must not reopen it, so a
        /// toggle during or just after a close is swallowed.
        void Toggle();

        void ShowFlyout();
        void HideFlyout();

        /// Keeps the panel open when it loses focus, for screenshots.
        void Pin(bool pinned) { m_pinned = pinned; }

        bool IsOpen() const { return m_visible; }

    private:
        struct Row
        {
            std::shared_ptr<::AstroDimmer::Displays::DisplayItem> Item;
            int ChangedToken{};
            Microsoft::UI::Xaml::Controls::FontIcon StageIcon{ nullptr };
            Microsoft::UI::Xaml::Controls::Slider Brightness{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock BrightnessText{ nullptr };
            Microsoft::UI::Xaml::FrameworkElement ContrastRow{ nullptr };
            Microsoft::UI::Xaml::Controls::Slider Contrast{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock ContrastText{ nullptr };
            bool Updating{ false };
        };

        struct Placement
        {
            RECT Work;
            double Scale;
            int Edge; // Core::ScreenEdge
        };

        void BuildRows();
        void ClearRows();
        void Refresh(Row& row);
        void UpdateStageGlyphs();
        void UpdateStatus();

        Placement CurrentPlacement() const;
        Windows::Graphics::PointInt32 RestingPosition(Placement const& placement, int width, int height) const;
        Windows::Graphics::PointInt32 SlideOrigin(Placement const& placement, Windows::Graphics::PointInt32 rest) const;
        int MeasuredHeight();
        void RepositionIfVisible();

        void Animate(Windows::Graphics::PointInt32 from, Windows::Graphics::PointInt32 to, double durationMs,
                     double (*ease)(double), std::function<void()> done);
        void OnRendering();

        HWND m_hwnd{};
        bool m_visible{ false };
        bool m_closing{ false };
        bool m_pinned{ false };
        double m_closeStartedAt{ -1e9 };
        double m_showStartedAt{ 0 };
        bool m_frameLogged{ true };
        int m_width{ 0 };
        int m_height{ 0 };

        std::wstring m_status;
        std::vector<std::unique_ptr<Row>> m_rows;

        struct Animation
        {
            Windows::Graphics::PointInt32 From;
            Windows::Graphics::PointInt32 To;
            double Start;
            double Duration;
            double (*Ease)(double);
            std::function<void()> Done;
        };
        std::optional<Animation> m_animation;
        event_token m_rendering{};
    };
}

namespace winrt::AstroDimmer::factory_implementation
{
    struct FlyoutWindow : FlyoutWindowT<FlyoutWindow, implementation::FlyoutWindow>
    {
    };
}
