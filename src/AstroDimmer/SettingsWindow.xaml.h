#pragma once

#include "SettingsWindow.g.h"
#include "AstroEngine.h"
#include "Displays/DisplayItem.h"

namespace winrt::AstroDimmer::implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow();

        void RestoreAndActivate();

    private:
        void Load();
        void OnFormChanged();
        void ReadForm();
        void UpdateStatus();
        void UpdateSteppers(std::optional<::AstroDimmer::Core::AstroEngine::Boundaries> const& boundaries);
        void UpdateCoordinates();
        void SetDetectStatus(std::wstring const& text);
        fire_and_forget DetectAsync(bool automatic);
        void BuildDisplayCards();
        void ClearDisplayCards();
        void SetToggle(Microsoft::UI::Xaml::Controls::ToggleSwitch const& toggle,
                       Microsoft::UI::Xaml::Controls::TextBlock const& state, bool on);

        bool m_loading{ true };
        bool m_detecting{ false };
        bool m_windowsLocationAvailable{ true };
        HICON m_icon{};
        int m_displaysToken{};
        int m_stageToken{};
        int m_settingsToken{};

        /// The live rows follow their display, so each card holds a
        /// subscription that has to go when the cards are rebuilt.
        std::vector<std::pair<std::shared_ptr<::AstroDimmer::Displays::DisplayItem>, int>> m_itemTokens;
    };
}

namespace winrt::AstroDimmer::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}
