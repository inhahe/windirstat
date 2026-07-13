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

#pragma once

#include "pch.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

//
// CHashCache
//
// A durable, cross-session cache of computed file hashes used by the duplicate
// finder. Hashes are computed in tiers (a small prefix, a medium prefix, then the
// whole file). Persisting them lets a later duplicate scan resume without re-reading
// file bytes, even after the application is closed or an intervening scan is run that
// did not look for duplicates.
//
// Cache entries are keyed by the file's full path (case-insensitive) and validated by
// the file's logical size and last-write time, both of which the scan already reads
// from disk, so a lookup never performs any extra I/O. If either differs from the
// cached value the entry is treated as a miss (the file changed) and re-hashed.
//
// The persisted file also records the NTFS file reference (index) for each entry so a
// future USN-journal-based invalidation pass can precisely purge changed files.
//
class CHashCache
{
public:
    enum HashTier : std::uint8_t { TierSmall = 0, TierMedium = 1, TierLarge = 2, TierCount = 3 };

    static CHashCache& Get();

    // Returns the cached hash for the given file/tier if present and still valid
    // (size and last-write time match), otherwise an empty vector.
    std::vector<BYTE> Lookup(const std::wstring& path, ULONGLONG size, FILETIME lastWrite, HashTier tier);

    // Stores a freshly computed hash for the given file/tier.
    void Store(const std::wstring& path, ULONGLONG size, FILETIME lastWrite,
        ULONGLONG fileIndex, HashTier tier, const std::vector<BYTE>& hash);

    // Loads the cache from disk. Safe to call once at startup. Any entries that were
    // hashed with a different algorithm, are too old, or exceed the size cap are
    // dropped. Silently starts empty if no valid cache file exists.
    void Load();

    // Persists the cache to disk if it changed since the last save.
    void Save();

    // Drops all cached entries (both in memory and, on the next Save, on disk).
    void Clear();

private:
    CHashCache() = default;

    struct Entry
    {
        ULONGLONG size = 0;
        FILETIME lastWrite{};
        ULONGLONG fileIndex = 0;
        ULONGLONG lastUsed = 0; // FILETIME-as-ULONGLONG of last store/hit, for pruning
        std::vector<BYTE> hashes[TierCount];
    };

    static std::wstring GetCacheFilePath();
    static ULONGLONG NowAsUInt64();

    std::shared_mutex m_mutex;
    std::unordered_map<std::wstring, Entry> m_entries; // key = lowercase path
    bool m_dirty = false;
    bool m_loaded = false;
    int m_algorithm = -1; // hash algorithm the in-memory entries belong to
};
