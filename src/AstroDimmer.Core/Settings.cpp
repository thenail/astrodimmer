#include "pch.h"
#include "Settings.h"

#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace AstroDimmer::Core
{
    namespace
    {
        std::wstring RoamingAppData()
        {
            PWSTR path = nullptr;
            std::wstring result;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
                result = path;
            CoTaskMemFree(path);
            return result;
        }

        std::optional<std::string> ReadAllBytes(std::wstring const& path)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return std::nullopt;

            LARGE_INTEGER size{};
            std::optional<std::string> result;

            // A settings file larger than this is not a settings file.
            if (GetFileSizeEx(file, &size) && size.QuadPart < 16 * 1024 * 1024)
            {
                std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
                DWORD read = 0;
                if (::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                {
                    bytes.resize(read);
                    result = std::move(bytes);
                }
            }

            CloseHandle(file);
            return result;
        }

        bool FileExists(std::wstring const& path)
        {
            DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }

        int ReadInt(Json::Value const* object, wchar_t const* name, int fallback)
        {
            if (!object) return fallback;
            auto value = object->Find(name);
            return value && value->AsInt() ? *value->AsInt() : fallback;
        }

        double ReadDouble(Json::Value const* object, wchar_t const* name, double fallback)
        {
            if (!object) return fallback;
            auto value = object->Find(name);
            return value && value->AsNumber() ? *value->AsNumber() : fallback;
        }

        bool ReadBool(Json::Value const* object, wchar_t const* name, bool fallback)
        {
            if (!object) return fallback;
            auto value = object->Find(name);
            return value && value->AsBool() ? *value->AsBool() : fallback;
        }
    }

    std::wstring AppSettings::DefaultDirectory()
    {
        return RoamingAppData() + L"\\AstroDimmer";
    }

    std::wstring AppSettings::DefaultPath()
    {
        return DefaultDirectory() + L"\\settings.json";
    }

    std::wstring AppSettings::GlimmerPath()
    {
        return RoamingAppData() + L"\\Glimmer\\settings.json";
    }

    AppSettings AppSettings::LoadDefault()
    {
        auto path = DefaultPath();
        if (!FileExists(path) && FileExists(GlimmerPath()))
            return Load(GlimmerPath());

        return Load(path);
    }

    AppSettings AppSettings::Load(std::wstring const& path)
    {
        auto bytes = ReadAllBytes(path);
        if (!bytes) return {};

        auto root = Json::Parse(Json::FromUtf8(*bytes));

        // Unreadable or malformed: start clean rather than fail to launch.
        if (!root) return {};

        return FromJson(*root);
    }

    AppSettings AppSettings::FromJson(Json::Value const& root)
    {
        AppSettings settings;
        auto& a = settings.Astro;

        if (auto astro = root.Find(L"Astro"))
        {
            a.Enabled = ReadBool(astro, L"Enabled", a.Enabled);
            a.Latitude = ReadDouble(astro, L"Latitude", a.Latitude);
            a.Longitude = ReadDouble(astro, L"Longitude", a.Longitude);
            a.DayOffset = ReadInt(astro, L"DayOffset", a.DayOffset);
            a.NightOffset = ReadInt(astro, L"NightOffset", a.NightOffset);
            a.DayBrightness = ReadInt(astro, L"DayBrightness", a.DayBrightness);
            a.NightBrightness = ReadInt(astro, L"NightBrightness", a.NightBrightness);
            a.FadeEnabled = ReadBool(astro, L"FadeEnabled", a.FadeEnabled);
            a.FadeMinutes = ReadInt(astro, L"FadeMinutes", a.FadeMinutes);

            if (auto perDisplay = astro->Find(L"PerDisplay"))
            {
                for (auto const& [key, value] : perDisplay->Members())
                {
                    DisplayLevels levels;
                    levels.Day = ReadInt(&value, L"Day", levels.Day);
                    levels.Night = ReadInt(&value, L"Night", levels.Night);
                    levels.Contrast = ReadBool(&value, L"Contrast", levels.Contrast);
                    levels.DayContrast = ReadInt(&value, L"DayContrast", levels.DayContrast);
                    levels.NightContrast = ReadInt(&value, L"NightContrast", levels.NightContrast);
                    a.PerDisplay[key] = levels;
                }
            }
        }

        if (auto hidden = root.Find(L"HiddenDisplays"))
            for (auto const& item : hidden->Items())
                if (auto key = item.AsString())
                    settings.HiddenDisplays.insert(*key);

        // A file without the list predates it (or is Glimmer's), and its
        // displays have been under the schedule's control.
        auto known = root.Find(L"KnownDisplays");
        settings.TracksKnownDisplays = known != nullptr;
        if (known)
            for (auto const& item : known->Items())
                if (auto key = item.AsString())
                    settings.KnownDisplays.insert(*key);

        return settings;
    }

    Json::Value AppSettings::ToJson() const
    {
        auto astro = Json::Value::MakeObject();
        astro.Set(L"Enabled", Astro.Enabled);
        astro.Set(L"Latitude", Astro.Latitude);
        astro.Set(L"Longitude", Astro.Longitude);
        astro.Set(L"DayOffset", Astro.DayOffset);
        astro.Set(L"NightOffset", Astro.NightOffset);
        astro.Set(L"DayBrightness", Astro.DayBrightness);
        astro.Set(L"NightBrightness", Astro.NightBrightness);

        auto perDisplay = Json::Value::MakeObject();
        for (auto const& [key, levels] : Astro.PerDisplay)
        {
            auto entry = Json::Value::MakeObject();
            entry.Set(L"Day", levels.Day);
            entry.Set(L"Night", levels.Night);
            entry.Set(L"Contrast", levels.Contrast);
            entry.Set(L"DayContrast", levels.DayContrast);
            entry.Set(L"NightContrast", levels.NightContrast);
            perDisplay.Set(key, std::move(entry));
        }
        astro.Set(L"PerDisplay", std::move(perDisplay));

        astro.Set(L"FadeEnabled", Astro.FadeEnabled);
        astro.Set(L"FadeMinutes", Astro.FadeMinutes);

        auto hidden = Json::Value::MakeArray();
        for (auto const& key : HiddenDisplays)
            hidden.Append(key);

        auto root = Json::Value::MakeObject();
        root.Set(L"Astro", std::move(astro));
        root.Set(L"HiddenDisplays", std::move(hidden));

        // Left out until complete: writing a partial list would mark an old
        // file as tracked with displays missing, and they would be seeded
        // from levels the schedule set.
        if (TracksKnownDisplays)
        {
            auto known = Json::Value::MakeArray();
            for (auto const& key : KnownDisplays)
                known.Append(key);
            root.Set(L"KnownDisplays", std::move(known));
        }

        return root;
    }

    bool AppSettings::Save(std::optional<std::wstring> const& path) const
    {
        auto target = path.value_or(DefaultPath());

        auto directory = target.substr(0, target.find_last_of(L'\\'));
        SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);

        auto temp = target + L".tmp";
        auto bytes = Json::ToUtf8(Json::Write(ToJson()));

        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                  && written == bytes.size();
        CloseHandle(file);

        // Losing a preference is preferable to crashing the tray app.
        if (!ok)
        {
            DeleteFileW(temp.c_str());
            return false;
        }

        return MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
}
