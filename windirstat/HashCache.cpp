// WinDirStat - Directory Statistics
// Copyright © WinDirStat Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "pch.h"
#include "HashCache.h"
#include "Options.h"
#include "HelpersInterface.h"
#include "WinDirStat.h"

#include <filesystem>
#include <fstream>

namespace
{
    constexpr char CACHE_MAGIC[4] = { 'W', 'D', 'H', 'C' };
    constexpr std::uint32_t CACHE_VERSION = 1;

    // Entries not used within this many days are dropped on load so the cache does
    // not grow without bound as files come and go.
    constexpr ULONGLONG PRUNE_AGE_100NS = 60ull * 24 * 60 * 60 * 10'000'000ull;

    // Upper bound on retained entries; the oldest are discarded first.
    constexpr size_t MAX_ENTRIES = 4'000'000;

    ULONGLONG FileTimeToUInt64(const FILETIME ft) noexcept
    {
        return static_cast<ULONGLONG>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
    }

    FILETIME UInt64ToFileTime(const ULONGLONG v) noexcept
    {
        return { static_cast<DWORD>(v & 0xFFFFFFFFull), static_cast<DWORD>(v >> 32) };
    }

    template <typename T> void WritePod(std::ofstream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    template <typename T> bool ReadPod(std::ifstream& in, T& value)
    {
        return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
    }
}

CHashCache& CHashCache::Get()
{
    static CHashCache instance;
    return instance;
}

ULONGLONG CHashCache::NowAsUInt64()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return FileTimeToUInt64(ft);
}

std::wstring CHashCache::GetCacheFilePath()
{
    // In portable mode keep the cache next to the executable/ini so the app leaves no
    // footprint elsewhere; otherwise store it under the user's local app data.
    if (CDirStatApp::InPortableMode())
    {
        return GetAppFileName(L"cache");
    }

    PWSTR localAppData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData != nullptr)
    {
        dir = localAppData;
    }
    if (localAppData != nullptr) CoTaskMemFree(localAppData);
    if (dir.empty()) return GetAppFileName(L"cache");

    dir += L"\\WinDirStat";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + L"\\hashcache.bin";
}

std::vector<BYTE> CHashCache::Lookup(const std::wstring& path, const ULONGLONG size,
    const FILETIME lastWrite, const HashTier tier)
{
    const std::wstring key = MakeLower(path);

    {
        std::shared_lock lock(m_mutex);
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) return {};

        const Entry& entry = it->second;
        if (entry.size != size ||
            FileTimeToUInt64(entry.lastWrite) != FileTimeToUInt64(lastWrite)) return {};

        const auto& hash = entry.hashes[tier];
        if (hash.empty()) return {};

        // Return a copy while holding the shared lock so the caller is unaffected by
        // concurrent evictions.
        return hash;
    }
}

void CHashCache::Store(const std::wstring& path, const ULONGLONG size, const FILETIME lastWrite,
    const ULONGLONG fileIndex, const HashTier tier, const std::vector<BYTE>& hash)
{
    if (hash.empty()) return;

    const std::wstring key = MakeLower(path);

    std::unique_lock lock(m_mutex);
    Entry& entry = m_entries[key];

    // If the file identity changed since a previous entry, discard the stale tiers.
    if (entry.size != size || FileTimeToUInt64(entry.lastWrite) != FileTimeToUInt64(lastWrite))
    {
        for (auto& h : entry.hashes) h.clear();
    }

    entry.size = size;
    entry.lastWrite = lastWrite;
    entry.fileIndex = fileIndex;
    entry.lastUsed = NowAsUInt64();
    entry.hashes[tier] = hash;
    m_dirty = true;
}

void CHashCache::Load()
{
    std::unique_lock lock(m_mutex);
    if (m_loaded) return;
    m_loaded = true;
    m_algorithm = COptions::FileHashAlgorithm;

    std::ifstream in(GetCacheFilePath(), std::ios::binary);
    if (!in) return;

    char magic[4] = {};
    std::uint32_t version = 0;
    std::uint32_t algorithm = 0;
    std::uint64_t count = 0;
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, CACHE_MAGIC, sizeof(magic)) != 0) return;
    if (!ReadPod(in, version) || version != CACHE_VERSION) return;
    if (!ReadPod(in, algorithm)) return;
    if (!ReadPod(in, count)) return;

    // Hashes are algorithm-specific; a changed algorithm invalidates the whole file.
    if (static_cast<int>(algorithm) != m_algorithm) return;

    const ULONGLONG now = NowAsUInt64();

    for (std::uint64_t i = 0; i < count; ++i)
    {
        std::uint32_t pathLen = 0;
        if (!ReadPod(in, pathLen) || pathLen > 32768) return;
        std::wstring path(pathLen, L'\0');
        if (pathLen > 0 && !in.read(reinterpret_cast<char*>(path.data()), static_cast<std::streamsize>(pathLen) * sizeof(wchar_t))) return;

        Entry entry;
        std::uint64_t sizeVal = 0, mtimeVal = 0, indexVal = 0, lastUsedVal = 0;
        if (!ReadPod(in, sizeVal) || !ReadPod(in, mtimeVal) ||
            !ReadPod(in, indexVal) || !ReadPod(in, lastUsedVal)) return;
        entry.size = sizeVal;
        entry.lastWrite = UInt64ToFileTime(mtimeVal);
        entry.fileIndex = indexVal;
        entry.lastUsed = lastUsedVal;

        bool anyHash = false;
        for (auto& h : entry.hashes)
        {
            std::uint8_t hlen = 0;
            if (!ReadPod(in, hlen) || hlen > 64) return;
            if (hlen > 0)
            {
                h.resize(hlen);
                if (!in.read(reinterpret_cast<char*>(h.data()), hlen)) return;
                anyHash = true;
            }
        }

        // Drop entries that are stale (too old) or carry no hashes.
        if (!anyHash) continue;
        if (now > entry.lastUsed && now - entry.lastUsed > PRUNE_AGE_100NS) continue;

        m_entries.emplace(MakeLower(path), std::move(entry));
    }
}

void CHashCache::Save()
{
    std::unique_lock lock(m_mutex);
    if (!m_dirty) return;

    // Enforce the size cap by dropping the least-recently-used entries.
    if (m_entries.size() > MAX_ENTRIES)
    {
        std::vector<std::pair<ULONGLONG, const std::wstring*>> byAge;
        byAge.reserve(m_entries.size());
        for (const auto& [key, entry] : m_entries) byAge.emplace_back(entry.lastUsed, &key);
        std::ranges::nth_element(byAge, byAge.begin() + MAX_ENTRIES,
            [](const auto& a, const auto& b) { return a.first > b.first; });
        std::unordered_map<std::wstring, Entry> trimmed;
        trimmed.reserve(MAX_ENTRIES);
        for (size_t i = 0; i < MAX_ENTRIES; ++i) trimmed.emplace(*byAge[i].second, std::move(m_entries[*byAge[i].second]));
        m_entries = std::move(trimmed);
    }

    const std::wstring path = GetCacheFilePath();
    const std::wstring tmp = path + L".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;

        out.write(CACHE_MAGIC, sizeof(CACHE_MAGIC));
        WritePod(out, CACHE_VERSION);
        WritePod(out, static_cast<std::uint32_t>(m_algorithm));
        WritePod(out, static_cast<std::uint64_t>(m_entries.size()));

        for (const auto& [key, entry] : m_entries)
        {
            WritePod(out, static_cast<std::uint32_t>(key.size()));
            if (!key.empty()) out.write(reinterpret_cast<const char*>(key.data()),
                static_cast<std::streamsize>(key.size()) * sizeof(wchar_t));
            WritePod(out, static_cast<std::uint64_t>(entry.size));
            WritePod(out, FileTimeToUInt64(entry.lastWrite));
            WritePod(out, static_cast<std::uint64_t>(entry.fileIndex));
            WritePod(out, static_cast<std::uint64_t>(entry.lastUsed));
            for (const auto& h : entry.hashes)
            {
                WritePod(out, static_cast<std::uint8_t>(h.size()));
                if (!h.empty()) out.write(reinterpret_cast<const char*>(h.data()),
                    static_cast<std::streamsize>(h.size()));
            }
        }

        if (!out) return; // leave any previous cache intact on write failure
    }

    // Atomically replace the previous cache.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }

    m_dirty = false;
}

void CHashCache::Clear()
{
    std::unique_lock lock(m_mutex);
    if (!m_entries.empty()) m_dirty = true;
    m_entries.clear();
}
