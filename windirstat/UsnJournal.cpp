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
#include "UsnJournal.h"
#include "SmartPointer.h"

#include <winioctl.h>

namespace
{
    // Opens a raw volume handle ("\\.\C:") from a drive-letter root ("C:\").
    // Requires FILE_READ_DATA, which in practice means the process must be elevated.
    HANDLE OpenVolume(const std::wstring& volumeRoot)
    {
        std::wstring root = volumeRoot;
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        if (root.size() < 2 || root[1] != L':') return INVALID_HANDLE_VALUE;

        const std::wstring devicePath = L"\\\\.\\" + root.substr(0, 2);
        return CreateFile(devicePath.c_str(), FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    // Reasons that make a previously computed content hash - or the cached path - invalid.
    constexpr DWORD ReasonMask =
        USN_REASON_DATA_OVERWRITE | USN_REASON_DATA_EXTEND | USN_REASON_DATA_TRUNCATION |
        USN_REASON_NAMED_DATA_OVERWRITE | USN_REASON_NAMED_DATA_EXTEND | USN_REASON_NAMED_DATA_TRUNCATION |
        USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME | USN_REASON_RENAME_NEW_NAME |
        USN_REASON_REPARSE_POINT_CHANGE;
}

std::optional<UsnJournal::JournalState> UsnJournal::Query(const std::wstring& volumeRoot)
{
    SmartPointer h(CloseHandle, OpenVolume(volumeRoot));
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;

    USN_JOURNAL_DATA_V0 data{};
    DWORD bytes = 0;
    if (DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytes, nullptr) == 0)
    {
        return std::nullopt;
    }

    return JournalState{ data.UsnJournalID, data.LowestValidUsn, data.NextUsn };
}

bool UsnJournal::ReadChanges(const std::wstring& volumeRoot, const ULONGLONG journalId,
    const LONGLONG startUsn, std::unordered_set<ULONGLONG>& changedFileIds)
{
    SmartPointer h(CloseHandle, OpenVolume(volumeRoot));
    if (h == INVALID_HANDLE_VALUE) return false;

    READ_USN_JOURNAL_DATA_V0 read{};
    read.StartUsn = startUsn;
    read.ReasonMask = 0xFFFFFFFF; // return every reason; we filter with ReasonMask below
    read.ReturnOnlyOnClose = FALSE;
    read.Timeout = 0;
    read.BytesToWaitFor = 0;
    read.UsnJournalID = journalId;

    // A V0 read request yields USN_RECORD_V2 records (64-bit file references), which is
    // exactly what NTFS uses; only ReFS needs the wider V3 records, and ReFS has no MFT
    // for this cache to key on.
    std::vector<BYTE> buffer(64 * 1024);
    for (;;)
    {
        DWORD returned = 0;
        if (DeviceIoControl(h, FSCTL_READ_USN_JOURNAL, &read, sizeof(read),
            buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr) == 0)
        {
            return false;
        }

        // The record buffer is prefixed by the USN to resume the next read from.
        if (returned <= sizeof(USN)) break;
        const USN resumeUsn = *reinterpret_cast<const USN*>(buffer.data());

        const BYTE* p = buffer.data() + sizeof(USN);
        const BYTE* const end = buffer.data() + returned;
        while (p + sizeof(USN_RECORD_V2) <= end)
        {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(p);
            if (record->RecordLength == 0 || p + record->RecordLength > end) break;

            if ((record->Reason & ReasonMask) != 0)
            {
                changedFileIds.insert(record->FileReferenceNumber & SegmentMask);
            }
            p += record->RecordLength;
        }

        // No more records were available at this point in the journal.
        if (resumeUsn == read.StartUsn) break;
        read.StartUsn = resumeUsn;
    }

    return true;
}
