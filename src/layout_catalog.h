#pragma once
#include "protocol.h"
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace dock {
inline std::wstring LocaleValue(WORD language, LCTYPE field) {
    wchar_t value[256]{};
    if (!GetLocaleInfoW(MAKELCID(language, SORT_DEFAULT), field, value, _countof(value))) return {};
    return value;
}
inline std::wstring Upper(std::wstring value) {
    for (auto& ch : value) ch = static_cast<wchar_t>(towupper(ch));
    return value;
}
inline std::wstring RegistryString(HKEY key, const wchar_t* name) {
    wchar_t value[256]{}; DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}
struct KeyboardName { DWORD klid{}, layoutId{}; std::wstring name; };
inline std::vector<KeyboardName> KeyboardNames() {
    std::vector<KeyboardName> result;
    HKEY parent{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0, KEY_READ, &parent)) return result;
    for (DWORD index = 0;; ++index) {
        wchar_t name[128]{}; DWORD length = _countof(name);
        LONG error = RegEnumKeyExW(parent, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) continue;
        HKEY key{};
        if (RegOpenKeyExW(parent, name, 0, KEY_READ, &key)) continue;
        KeyboardName entry;
        entry.klid = wcstoul(name, nullptr, 16);
        entry.layoutId = wcstoul(RegistryString(key, L"Layout Id").c_str(), nullptr, 16);
        entry.name = RegistryString(key, L"Layout Text");
        RegCloseKey(key); result.push_back(std::move(entry));
    }
    RegCloseKey(parent); return result;
}
// Query size first and retry if Windows adds a layout between the two calls.
inline std::vector<uint64_t> InstalledLayouts() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        int count = GetKeyboardLayoutList(0, nullptr);
        if (count <= 0) return {};
        std::vector<HKL> handles(static_cast<size_t>(count));
        int written = GetKeyboardLayoutList(count, handles.data());
        if (written <= 0) return {};
        if (GetKeyboardLayoutList(0, nullptr) > count) continue;
        std::vector<uint64_t> result;
        for (int i = 0; i < written; ++i) {
            uint64_t id = reinterpret_cast<uintptr_t>(handles[i]);
            if (id && std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        }
        return result;
    }
    return {};
}
struct LanguageOrder { WORD language{}; std::vector<DWORD> layouts; };
// CTF stores the switcher's language order separately from the HKL load order.
// These registry keys are read-only hints; missing/stale entries fall back to loaded HKLs.
inline std::vector<LanguageOrder> WindowsOrder() {
    std::vector<std::pair<unsigned long, WORD>> languages;
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\CTF\\SortOrder\\Language", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[64]{}, value[64]{}; DWORD length = _countof(name), bytes = sizeof(value), type{};
            LONG error = RegEnumValueW(key, index, name, &length, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
            if (error == ERROR_NO_MORE_ITEMS) break;
            if (error != ERROR_SUCCESS || type != REG_SZ) continue;
            wchar_t* end{}; unsigned long ordinal = wcstoul(name, &end, 16);
            if (!name[0] || *end) continue;
            value[_countof(value) - 1] = 0;
            unsigned long language = wcstoul(value, &end, 16);
            if (value[0] && !*end && language <= 0xffff) languages.emplace_back(ordinal, static_cast<WORD>(language));
        }
        RegCloseKey(key);
    }
    std::sort(languages.begin(), languages.end());
    if (languages.empty()) {
        DWORD bytes{};
        constexpr auto path = L"Control Panel\\International\\User Profile";
        if (RegGetValueW(HKEY_CURRENT_USER, path, L"Languages", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS) {
            std::vector<wchar_t> names(bytes / sizeof(wchar_t) + 2, 0);
            if (RegGetValueW(HKEY_CURRENT_USER, path, L"Languages", RRF_RT_REG_MULTI_SZ, nullptr, names.data(), &bytes) == ERROR_SUCCESS) {
                for (const wchar_t* tag = names.data(); *tag; tag += wcslen(tag) + 1) {
                    wchar_t resolved[LOCALE_NAME_MAX_LENGTH]{};
                    if (ResolveLocaleName(tag, resolved, _countof(resolved))) {
                        LCID locale = LocaleNameToLCID(resolved, 0);
                        if (locale) languages.emplace_back(static_cast<unsigned long>(languages.size()), LANGIDFROMLCID(locale));
                    }
                }
            }
        }
    }
    std::vector<LanguageOrder> result;
    for (const auto& item : languages) {
        LanguageOrder order; order.language = item.second;
        wchar_t path[256]{};
        swprintf_s(path, L"Software\\Microsoft\\CTF\\SortOrder\\AssemblyItem\\0x%08x\\{34745C63-B2F0-4784-8B67-5E12C8701A31}", item.second);
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) == ERROR_SUCCESS) {
            std::vector<std::pair<unsigned long, DWORD>> profiles;
            for (DWORD index = 0;; ++index) {
                wchar_t name[64]{}; DWORD length = _countof(name);
                LONG error = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
                if (error == ERROR_NO_MORE_ITEMS) break;
                if (error != ERROR_SUCCESS) continue;
                wchar_t* end{}; unsigned long ordinal = wcstoul(name, &end, 16);
                if (!name[0] || *end) continue;
                DWORD layout{}, bytes = sizeof(layout);
                if (RegGetValueW(key, name, L"KeyboardLayout", RRF_RT_REG_DWORD, nullptr, &layout, &bytes) == ERROR_SUCCESS && layout)
                    profiles.emplace_back(ordinal, layout);
            }
            RegCloseKey(key); std::sort(profiles.begin(), profiles.end());
            for (const auto& profile : profiles) order.layouts.push_back(profile.second);
        }
        result.push_back(std::move(order));
    }
    return result;
}
inline std::vector<uint64_t> OrderLayouts(const std::vector<uint64_t>& installed, const std::vector<LanguageOrder>& order) {
    std::vector<uint64_t> result;
    auto append = [&](uint64_t id) { if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id); };
    for (const auto& language : order) {
        for (DWORD profile : language.layouts) for (uint64_t id : installed)
            if (static_cast<DWORD>(id) == profile) append(id);
        for (uint64_t id : installed) if (LOWORD(id) == language.language) append(id);
    }
    for (uint64_t id : installed) append(id);
    return result;
}
inline std::vector<Layout> MakeCatalog(const std::vector<uint64_t>& ids) {
    const auto keyboards = KeyboardNames();
    std::vector<Layout> result;
    std::vector<std::wstring> bases, regions;
    for (uint64_t id : ids) {
        WORD language = LOWORD(id), device = HIWORD(id);
        std::wstring base = Upper(LocaleValue(language, LOCALE_SISO639LANGNAME));
        if (base.empty()) base = L"KB";
        std::wstring region = Upper(LocaleValue(language, LOCALE_SISO3166CTRYNAME));
        std::wstring name = LocaleValue(language, LOCALE_SENGLISHDISPLAYNAME);
        if (name.empty()) name = L"Keyboard layout";
        const KeyboardName* keyboard = nullptr;
        for (const auto& candidate : keyboards) {
            if ((device & 0xf000) == 0xf000) {
                if (candidate.layoutId && candidate.layoutId == static_cast<DWORD>(device & 0x0fff) && LOWORD(candidate.klid) == language) { keyboard = &candidate; break; }
            } else if (candidate.klid == device) { keyboard = &candidate; break; }
        }
        if (keyboard && !keyboard->name.empty()) name += L" — " + keyboard->name;
        else {
            wchar_t suffix[24]{}; swprintf_s(suffix, L" [%08X]", static_cast<unsigned>(id & 0xffffffff));
            name += suffix;
        }
        Layout entry{}; entry.id = id;
        wcsncpy_s(entry.name, name.c_str(), _TRUNCATE);
        result.push_back(entry); bases.push_back(std::move(base)); regions.push_back(std::move(region));
    }
    std::vector<std::wstring> labels = bases;
    for (size_t i = 0; i < result.size(); ++i) {
        if (std::count(bases.begin(), bases.end(), bases[i]) > 1 && !regions[i].empty()) labels[i] += L"-" + regions[i];
    }
    for (size_t i = 0; i < result.size(); ++i) {
        std::wstring label = labels[i];
        if (std::count(labels.begin(), labels.end(), label) > 1) {
            size_t ordinal = 1;
            for (size_t j = 0; j < i; ++j) if (labels[j] == label) ++ordinal;
            label += std::to_wstring(ordinal);
        }
        wcsncpy_s(result[i].label, label.c_str(), _TRUNCATE);
    }
    return result;
}
}
