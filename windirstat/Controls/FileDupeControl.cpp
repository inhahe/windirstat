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
#include "ItemDupe.h"
#include "FileTreeView.h"

CFileDupeControl::CFileDupeControl() : CTreeListControl(COptions::DupeViewColumnOrder.Ptr(), COptions::DupeViewColumnWidths.Ptr(), LF_DUPELIST, false)
{
    m_singleton = this;
}

bool CFileDupeControl::GetAscendingDefault(const int column)
{
    return column == COL_ITEMDUP_NAME || column == COL_ITEMDUP_LAST_CHANGE;
}

BEGIN_MESSAGE_MAP(CFileDupeControl, CTreeListControl)
END_MESSAGE_MAP()

void CFileDupeControl::ProcessDuplicate(CItem* item, BlockingQueue<CItem*>* queue)
{
    if (!COptions::ScanForDuplicates) return;
    if (item->IsTypeOrFlag(ITRP_CLOUD) && COptions::SkipDupeDetectionCloudLinks)
    {
        // Show warning on the UI thread (not here — calling DoModal from a
        // worker thread with a UI-thread parent causes cross-thread SendMessage
        // that can deadlock with the timer-driven list-control updates).
        if (m_showCloudWarningOnThisScan)
        {
            m_showCloudWarningOnThisScan = false;
            CMainFrame::Get()->InvokeInMessageThread([this]
            {
                if (const auto [nID, isChecked] = CMessageBoxDlg::Show(
                    Localization::Lookup(IDS_DUPLICATES_WARNING),
                    Localization::Lookup(IDS_DONT_SHOW_AGAIN), false,
                    MB_OK | MB_ICONINFORMATION, this);
                    nID == IDOK && isChecked)
                {
                    COptions::ShowDupeDetectionCloudLinksWarning = false;
                }
            });
        }

        return;
    }

    // Fetch the configuration information based on file size
    const auto size = item->GetSizeLogical();
    auto [hashTracker, hashTrackerMutex, maxHashLevel] = [&]()->std::tuple
        <std::map<std::vector<BYTE>, std::vector<CItem*>>&, std::mutex&, ITEMTYPE>
    {
        if (size <= HashThreshold(ITHASH_SMALL)) return { m_trackerSmall, m_trackerSmallMutex, ITHASH_SMALL };
        if (size > HashThreshold(ITHASH_MEDIUM)) return { m_trackerLarge, m_trackerLargeMutex, ITHASH_LARGE };
        else return { m_trackerMedium, m_trackerMediumMutex, ITHASH_MEDIUM };
    }();

    // First see if there's more than one size of this file since there is no need to
    // hash if there is only a single file of this size
    std::vector<CItem*> hashSet;
    {
        std::scoped_lock lock(m_sizeTrackerMutex);
        auto& sizeSetLookup = m_sizeTracker[size];
        sizeSetLookup.emplace_back(item);
        hashSet.assign(sizeSetLookup.begin(), sizeSetLookup.end());
        if (sizeSetLookup.size() < 2) return;
    }

    // Now we have multiple files of the same size, so we need to hash them
    ITEMTYPE hashLevel = ITHASH_SMALL;
    std::map<std::vector<BYTE>, std::set<CItem*>> hashItemsWithDupes;
    for (std::unique_lock lock(hashTrackerMutex); !hashSet.empty();)
    {
        // Work on a snapshot of the current set
        std::set<CItem*> nextLevelSet;
        for (auto* itemToHash : std::vector(hashSet))
        {
            // Skip if already marked as unhashable or hashed at this level
            if (itemToHash->IsTypeOrFlag(ITHASH_SKIP, hashLevel)) continue;
            itemToHash->SetFlag(hashLevel);

            // Compute the hash for the file
            const auto hashSize = HashThreshold(hashLevel);
            lock.unlock();
            auto hash = itemToHash->GetFileHash(hashSize, queue);
            lock.lock();

            // Mark as bad if not hashable
            if (hash.empty())
            {
                itemToHash->SetFlag(ITHASH_SKIP);
                continue;
            }

            // Add this hash to the tracker
            auto& entry = hashTracker[hash];
            entry.emplace_back(itemToHash);
            auto view = entry | std::views::filter([&](const CItem* x)
                { return x->GetSizeLogical() == size; });
            std::vector subset(view.begin(), view.end());
            if (subset.size() < 2) continue;

            // See if this hash has duplicates
            if (hashLevel == maxHashLevel)
            {
                // Already at max level: record duplicates and stop
                hashItemsWithDupes[hash].insert(subset.begin(), subset.end());
            }
            else
            {
                // Schedule next-level hashing
                nextLevelSet.insert(subset.begin(), subset.end());
            }
        }

        hashSet.assign(nextLevelSet.begin(), nextLevelSet.end());
        if (hashSet.empty()) break;

        // Determine the appropriate hash level for this item based on what's already been hashed
        hashLevel = std::min(maxHashLevel,
            hashLevel == ITHASH_SMALL ? ITHASH_MEDIUM : ITHASH_LARGE);
    }

    // Lock once and lookup dupeParent before iterating
    if (hashItemsWithDupes.empty()) return;
    std::scoped_lock nodeLock(m_nodeTrackerMutex);
    for (const auto& [hash, itemsWithHash] : hashItemsWithDupes)
    {
        const auto nodeEntry = m_nodeTracker.find(hash);
        auto dupeParent = nodeEntry != m_nodeTracker.end() ? nodeEntry->second : nullptr;

        if (dupeParent == nullptr)
        {
            // Create new root item to hold these duplicates
            dupeParent = new CItemDupe(hash);
            m_pendingListAdds.push(std::make_pair(nullptr, dupeParent));
            m_nodeTracker.emplace(hash, dupeParent);
        }

        // Add all items under the same parent
        for (const auto& itemToAdd : itemsWithHash)
        {
            auto& hashParentNode = m_childTracker[dupeParent];
            if (hashParentNode.contains(itemToAdd)) continue;
            const auto dupeChild = new CItemDupe(itemToAdd);
            m_pendingListAdds.push(std::make_pair(dupeParent, dupeChild));
            hashParentNode.emplace(itemToAdd);
        }
    }
}

void CFileDupeControl::RebuildFromSavedHashes(const std::vector<std::pair<CItem*, std::wstring>>& itemHashes)
{
    if (m_rootItem == nullptr) return;

    // Group the restored items by their cached hash string, preserving first-seen order
    std::map<std::wstring, std::vector<CItem*>> groups;
    for (const auto& [item, hash] : itemHashes)
    {
        if (item == nullptr || hash.empty()) continue;
        groups[hash].push_back(item);
    }

    // Convert a lowercase hex string back into raw bytes for the node tracker key
    const auto hexToBytes = [](const std::wstring& hex) -> std::vector<BYTE>
    {
        std::vector<BYTE> bytes;
        if (hex.size() % 2 != 0) return bytes;
        bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            const auto nibble = [](wchar_t c) -> int
            {
                if (c >= L'0' && c <= L'9') return c - L'0';
                if (c >= L'a' && c <= L'f') return c - L'a' + 10;
                if (c >= L'A' && c <= L'F') return c - L'A' + 10;
                return -1;
            };
            const int hi = nibble(hex[i]);
            const int lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0) { bytes.clear(); break; }
            bytes.push_back(static_cast<BYTE>(hi << 4 | lo));
        }
        return bytes;
    };

    for (const auto& [hash, items] : groups)
    {
        // A single file with a given hash is not a duplicate
        if (items.size() < 2) continue;

        auto* dupeParent = new CItemDupe(hash);
        m_pendingListAdds.push(std::make_pair(nullptr, dupeParent));
        if (auto bytes = hexToBytes(hash); !bytes.empty())
            m_nodeTracker.emplace(std::move(bytes), dupeParent);

        auto& childSet = m_childTracker[dupeParent];
        for (auto* item : items)
        {
            const auto dupeChild = new CItemDupe(item);
            m_pendingListAdds.push(std::make_pair(dupeParent, dupeChild));
            childSet.emplace(item);

            // Mirror the state a live scan would leave behind so later edits
            // (e.g. deleting a file) clean the visual tree correctly.
            item->SetHashType(ITHASH_LARGE);
            m_sizeTracker[item->GetSizeLogical()].push_back(item);
        }
    }

    // Flush the pending additions into the visual tree
    SortItems();
}

void CFileDupeControl::SortItems()
{
    ASSERT(AfxGetThread() != nullptr);

    // Add items to the list
    if (!m_pendingListAdds.empty())
    {
        const CSetRedrawLock lock(this);
        std::pair<CItemDupe*, CItemDupe*> pair;
        while (m_pendingListAdds.pop(pair))
        {
            const auto& [parent, child] = pair;
            (parent == nullptr ? m_rootItem : parent)->AddDupeItemChild(child);
        }

    }

    CTreeListControl::SortItems();
}

void CFileDupeControl::RemoveItem(CItem* item)
{
    // Exit immediately if not doing duplicate detector
    if (!COptions::ScanForDuplicates) return;

    // Enumerate child items and mark all as unhashed
    std::vector queue({ item });
    while (!queue.empty())
    {
        const auto qitem = queue.back();
        queue.pop_back();
        if (qitem->IsTypeOrFlag(IT_FILE))
        {
            // Mark as all files as not being hashed anymore
            std::erase(m_sizeTracker[qitem->GetSizeLogical()], qitem);
            qitem->SetHashType(ITHASH_NONE, false);
        }
        else if (!qitem->IsLeaf())
        {
            std::ranges::copy(qitem->GetChildren(), std::back_inserter(queue));
        }
    }
    std::erase_if(m_sizeTracker, [](const auto& pair)
    {
        return pair.second.empty();
    });

    // Remove all unhashed files from hash trackers
    for (auto* hashTracker : { &m_trackerSmall, &m_trackerMedium, &m_trackerLarge })
    {
        for (auto& hashSet : *hashTracker | std::views::values)
        {
            // Skip if no matches of the item associated with this hash
            std::erase_if(hashSet, [](const auto& hashItem)
            {
                return !hashItem->IsTypeOrFlag(ITHASH_MASK);
            });
        }

        // Cleanup empty structures
        std::erase_if(*hashTracker, [](const auto& pair)
        {
            return pair.second.empty();
        });
    }

    // Cleanup any empty visual nodes in the list
    const CSetRedrawLock lock(this);
    for (auto nodeIter = m_nodeTracker.begin(); nodeIter != m_nodeTracker.end(); )
    {
        auto& [dupeParentKey, dupeParent] = *nodeIter;

        // Remove from child tracker
        auto& childItems = m_childTracker[dupeParent];
        for (auto childItem = childItems.begin(); childItem != childItems.end(); )
        {
            // Nothing to do if still marked as hashed
            if ((*childItem)->IsTypeOrFlag(ITHASH_MASK))
            {
                ++childItem;
                continue;
            }

            // Remove from child tracker and visual tree
            bool foundVisualChild = false;
            for (auto& visualChild : dupeParent->GetChildren())
            {
                if (visualChild->GetLinkedItem() == *childItem)
                {
                    dupeParent->RemoveDupeItemChild(visualChild);
                    foundVisualChild = true;
                    break;
                }
            }

            // Only erase from tracker when the visual child was actually removed;
            // if no visual match exists (pending add not yet flushed), leave the
            // tracker entry intact so it stays in sync with the visual tree.
            if (foundVisualChild)
                childItem = childItems.erase(childItem);
            else
                ++childItem;
        }

        // When only one child left, remove child item
        if (dupeParent->GetChildren().size() == 1)
        {
            dupeParent->RemoveDupeItemChild(dupeParent->GetChildren().front());
        }

        // When no children left, remove parent item
        if (dupeParent->GetChildren().empty())
        {
            m_rootItem->RemoveDupeItemChild(dupeParent);
            nodeIter = m_nodeTracker.erase(nodeIter);
        }
        else
        {
            ++nodeIter;
        }
    }
    std::erase_if(m_childTracker, [](const auto& pair)
    {
        return pair.second.size() <= 1;
    });
}

void CFileDupeControl::AfterDeleteAllItems()
{
    // Reset duplicate warning
    m_showCloudWarningOnThisScan = COptions::ShowDupeDetectionCloudLinksWarning;

    // Cleanup support lists
    m_pendingListAdds.clear();
    m_nodeTracker.clear();
    m_trackerSmall.clear();
    m_trackerMedium.clear();
    m_trackerLarge.clear();
    m_sizeTracker.clear();
    m_childTracker.clear();

    // Delete and recreate root item
    delete m_rootItem;
    m_rootItem = new CItemDupe();
    InsertItem(0, m_rootItem);
    m_rootItem->SetExpanded(true);
}
