#pragma once

#include <optional>
#include <set>
#include <string>
#include "AstroSettings.h"
#include "Json.h"

namespace AstroDimmer::Core
{
    /// Persisted configuration, stored as JSON under %APPDATA%\AstroDimmer.
    ///
    /// The format is Glimmer's, field for field, so a Glimmer settings file
    /// can be read as it stands - see LoadDefault.
    ///
    /// Loading is deliberately forgiving: a corrupt or unreadable file yields
    /// defaults rather than an error, because a tray utility that refuses to
    /// start over a bad settings file is worse than one that forgets a
    /// preference.
    struct AppSettings
    {
        AstroSettings Astro;

        /// Device keys the user has hidden from the flyout.
        std::set<std::wstring> HiddenDisplays;

        /// Device keys AstroDimmer has seen, so a display is given starting
        /// levels once - when it first appears - and never again.
        std::set<std::wstring> KnownDisplays;

        /// Whether KnownDisplays is complete. False only for a settings file
        /// from before it existed, whose displays were already being driven
        /// by the schedule (see AdoptDisplays). Not persisted: saving writes
        /// KnownDisplays, which makes it true on the next load.
        bool TracksKnownDisplays{ true };

        static std::wstring DefaultDirectory();
        static std::wstring DefaultPath();

        /// Where Glimmer kept its settings, for the one-time import.
        static std::wstring GlimmerPath();

        /// The settings at the default path. When there are none yet but
        /// Glimmer's exist, those are read instead - same format, same device
        /// keys - so moving to AstroDimmer keeps the location and every
        /// display's levels. They are written to AstroDimmer's own file on the
        /// first save; Glimmer's file is never touched.
        static AppSettings LoadDefault();

        static AppSettings Load(std::wstring const& path);

        /// Writes via a temporary file, so a crash mid-save cannot truncate
        /// the settings. Returns false if nothing could be written.
        bool Save(std::optional<std::wstring> const& path = std::nullopt) const;

        static AppSettings FromJson(Json::Value const& root);
        Json::Value ToJson() const;
    };
}
