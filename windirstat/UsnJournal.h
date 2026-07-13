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

#include <optional>
#include <unordered_set>

//
// UsnJournal
//
// Thin helpers over the NTFS USN change journal. The duplicate-hash cache uses them to
// precisely invalidate the entries of files whose contents changed (or that were deleted
// or renamed) since the cache was last saved - changes that the size/last-write-time
// check might miss (for example an in-place edit that restores the original timestamp).
//
// Opening a volume for journal access requires administrator rights, so every function
// fails gracefully (returns std::nullopt / false) when the journal cannot be read; the
// cache then simply falls back to its size/timestamp validation.
//
namespace UsnJournal
{
    struct JournalState
    {
        ULONGLONG journalId = 0; // identifies a specific journal instance
        LONGLONG firstUsn = 0;   // lowest USN the journal still retains
        LONGLONG nextUsn = 0;    // one past the newest record (the resume point)
    };

    // Queries the active journal on the volume rooted at volumeRoot (e.g. L"C:\\").
    // Returns the journal identity and USN range, or nullopt when the volume has no
    // journal or cannot be opened (e.g. not elevated, not NTFS, not mounted).
    std::optional<JournalState> Query(const std::wstring& volumeRoot);

    // Reads every change record on the volume from startUsn to the current end of the
    // journal, inserting the file reference numbers (masked to the 48-bit MFT segment
    // number) of files whose data changed, were deleted, or were renamed. Returns false
    // on any I/O failure. journalId guards against reading a different journal instance.
    bool ReadChanges(const std::wstring& volumeRoot, ULONGLONG journalId,
        LONGLONG startUsn, std::unordered_set<ULONGLONG>& changedFileIds);

    // Low 48 bits of an NTFS file reference isolate the MFT segment number, dropping the
    // sequence number that differs between the two finders' index representations.
    constexpr ULONGLONG SegmentMask = 0x0000'FFFF'FFFF'FFFFull;
}
