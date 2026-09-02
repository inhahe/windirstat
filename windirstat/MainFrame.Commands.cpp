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
#include "Filtering.h"
#include "TreeMapView.h"
#include "FlameGraphView.h"
#include "FileTabbedView.h"
#include "FileTreeView.h"
#include "DrawTextCache.h"
#include "ExtensionView.h"
#include "PageAdvanced.h"
#include "PageFiltering.h"
#include "PageCleanups.h"
#include "PageFileTree.h"
#include "PageTreeMap.h"
#include "PagePermissions.h"
#include "PageGeneral.h"
#include "PagePrompts.h"
#include "ProgressDlg.h"

constexpr auto ID_STATUSPANE_MODE_INDEX = 0;
constexpr auto ID_STATUSPANE_IDLE_INDEX = 1;
constexpr auto ID_STATUSPANE_SIZE_INDEX = 2;
constexpr auto ID_STATUSPANE_RAM_INDEX = 3;
constexpr auto ID_INACTIVE_GRAPH_PANE = 0xEB00; // Outside MFC's control-bar and splitter-pane ID ranges.

static void SetNativeMenuRadio(CCmdUI* command, const bool checked)
{
    command->SetCheck(checked);
    if (command->m_pMenu == nullptr || command->m_pSubMenu != nullptr) return;

    MENUITEMINFO info{ .cbSize = sizeof(MENUITEMINFO), .fMask = MIIM_FTYPE | MIIM_CHECKMARKS };
    if (!command->m_pMenu->GetMenuItemInfo(command->m_nIndex, &info, TRUE)) return;

    info.fType |= MFT_RADIOCHECK;
    info.hbmpChecked = nullptr;
    info.hbmpUnchecked = nullptr;
    command->m_pMenu->SetMenuItemInfo(command->m_nIndex, &info, TRUE);
}

void CMainFrame::OnInitMenuPopup(CMenu* pPopupMenu, const UINT nIndex, const BOOL bSysMenu)
{
    CFrameWndEx::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);

    if (const auto [explorerMenu, explorerMenuPos] = LocateNamedMenu(pPopupMenu,
        Localization::Lookup(IDS_MENU_EXPLORER_MENU), false); explorerMenu != nullptr)
    {
        // Add placeholder only
        if (explorerMenu->GetMenuItemCount() == 0)
        {
            explorerMenu->AppendMenu(MF_STRING | MF_DISABLED | MF_GRAYED, 0,
                Localization::Lookup(IDS_PROGRESS).c_str());
        }

        SetMenuItem(pPopupMenu, explorerMenuPos, true);
    }

    // If the menu being opened is populate it
    if (pPopupMenu->GetMenuItemCount() == 1 && pPopupMenu->GetMenuItemID(0) == 0)
    {
        while (pPopupMenu->GetMenuItemCount() > 0)
        {
            pPopupMenu->DeleteMenu(0, MF_BYPOSITION);
        }

        std::vector<std::wstring> paths;
        for (const auto& item : CWinDirStatModel::Get()->GetAllSelected())
        {
            paths.push_back(item->GetPath());
        }

        if (const CComPtr contextMenu = GetContextMenu(GetSafeHwnd(), paths);
            contextMenu != nullptr)
        {
            (void) contextMenu->QueryContextMenu(pPopupMenu->GetSafeHmenu(), 0,
                CONTENT_MENU_MINCMD, CONTENT_MENU_MAXCMD, CMF_NORMAL);
        }
    }

    // update cleanup menu if this is the cleanup submenu
    if (pPopupMenu->GetMenuState(ID_CLEANUP_EMPTY_BIN, MF_BYCOMMAND) != static_cast<UINT>(-1))
    {
        UpdateCleanupMenu(pPopupMenu);
    }

    // update tools menu - check if pPopupMenu is the Tools menu by looking for our operation submenus
    const auto [shadowCopyMenu, shadowCopyPos] = LocateNamedMenu(pPopupMenu, Localization::Lookup(IDS_MENU_SHADOW_COPY), false);
    if (shadowCopyMenu != nullptr)
    {
        UpdateToolsMenu(pPopupMenu);
    }
}

void CMainFrame::UpdateCleanupMenu(CMenu* menu, const bool triggerAsync)
{
    // Define menu items structure with cached values
    struct { ULONGLONG* count; ULONGLONG* bytes; UINT menuId; LPCWSTR prefix; } menuItems[] = {
        { &m_recycleBinItems, &m_recycleBinBytes, ID_CLEANUP_EMPTY_BIN, IDS_EMPTY_RECYCLEBIN.data() },
        { &m_shadowCopyCount, &m_shadowCopyBytes, ID_CLEANUP_REMOVE_SHADOW, IDS_MENU_REMOVE_SHADOW.data() }
    };

    // Update menu items using cached values (initially shows zeros or last cached values)
    for (const auto& [count, bytes, menuId, prefix] : menuItems)
    {
        const std::wstring label = Localization::Lookup(prefix) + ((*count == 1) ?
            Localization::Format(IDS_ONEITEMs, FormatBytes(*bytes)) :
            Localization::Format(IDS_sITEMSs, FormatCount(*count), FormatBytes(*bytes)));

        const UINT state = menu->GetMenuState(menuId, MF_BYCOMMAND);
        menu->ModifyMenu(menuId, MF_BYCOMMAND | MF_STRING, menuId, label.c_str());
        menu->EnableMenuItem(menuId, state);
    }

    UpdateDynamicMenuItems(menu);

    // Launch a detached thread to perform the queries
    if (triggerAsync) std::thread([this]()
    {
        // Query recycle bin and shadow copies
        QueryRecycleBin(m_recycleBinItems, m_recycleBinBytes);
        QueryShadowCopies(m_shadowCopyCount, m_shadowCopyBytes);

        // Use InvokeInMessageThread to update the menu on the UI thread
        InvokeInMessageThread([this]()
        {
            // Check if the menu is still valid and visible
            const auto [menuObj, menuPos] = LocateNamedMenu(GetMenu(), Localization::Lookup(IDS_MENU_CLEANUP), false);
            if (menuObj == nullptr || menuObj->GetMenuItemCount() <= 0) return;

            // Update menu items with the newly retrieved values
            UpdateCleanupMenu(menuObj, false);
        });
    }).detach();
}

void CMainFrame::QueryRecycleBin(ULONGLONG& items, ULONGLONG& bytes)
{
    items = 0;
    bytes = 0;

    for (const std::wstring & drive : GetDriveList({DRIVE_FIXED, DRIVE_REMOVABLE, DRIVE_RAMDISK}))
    {
        SHQUERYRBINFO qbi{ .cbSize = sizeof(qbi) };
        if (FAILED(::SHQueryRecycleBin((drive + L"\\").c_str(), &qbi)))
        {
            continue;
        }

        items += qbi.i64NumItems;
        bytes += qbi.i64Size;
    }
}

std::pair<CMenu*,int> CMainFrame::LocateNamedMenu(const CMenu* menu, const std::wstring & subMenuText, const bool removeItems) const
{
    // locate submenu
    CMenu* subMenu = nullptr;
    int subMenuPos = -1;
    for (const int i : std::views::iota(0, menu->GetMenuItemCount()))
    {
        CStringW menuString;
        if (menu->GetMenuString(i, menuString, MF_BYPOSITION) > 0 &&
            _wcsicmp(menuString, subMenuText.c_str()) == 0)
        {
            subMenu = menu->GetSubMenu(i);
            subMenuPos = i;
            break;
        }
    }

    // cleanup old items
    if (removeItems && subMenu != nullptr) while (subMenu->GetMenuItemCount() > 0)
        subMenu->DeleteMenu(0, MF_BYPOSITION);
    return { subMenu, subMenuPos };
}

void CMainFrame::UpdateDynamicMenuItems(CMenu* menu) const
{
    const auto& items = CWinDirStatModel::Get()->GetAllSelected();
    const bool scanReady = CWinDirStatModel::Get()->IsScanSettled();

    // get list of paths from items
    std::vector<std::wstring> paths;
    for (auto& item : items) paths.push_back(item->GetPath());

    // locate compress menu
    auto [compressMenu, compressMenuPos] = CMainFrame::LocateNamedMenu(menu, Localization::Lookup(IDS_MENU_COMPRESS_MENU), false);
    if (compressMenu && compressMenuPos >= 0)
    {
        // Check if any submenu items are enabled
        const int menuItemCount = compressMenu->GetMenuItemCount();
        const bool anyEnabled = std::ranges::any_of(std::views::iota(0, menuItemCount), [&](const int i)
        {
            CCmdUI state;
            state.m_nIndex = i;
            state.m_nIndexMax = menuItemCount;
            state.m_nID = compressMenu->GetMenuItemID(i);
            state.m_pMenu = compressMenu;
            state.DoUpdate(const_cast<CMainFrame*>(this), FALSE);
            return IsMenuEnabled(compressMenu, i);
        });

        SetMenuItem(menu, compressMenuPos, anyEnabled);
    }

    auto[customMenu, customMenuPos] = LocateNamedMenu(menu, Localization::Lookup(IDS_USER_DEFINED_CLEANUP));
    for (UINT iCurrent = 0; customMenu != nullptr && iCurrent < COptions::UserDefinedCleanups.size(); iCurrent++)
    {
        auto& udc = COptions::UserDefinedCleanups[iCurrent];
        if (!udc.Enabled) continue;

        std::wstring string = std::vformat(Localization::Lookup(IDS_UDCsCTRLd),
            std::make_wformat_args(udc.Title.Obj(), iCurrent));

        bool udcValid = scanReady && GetLogicalFocus() == LF_FILETREE && !items.empty();
        if (udcValid) for (const auto& item : items)
        {
            udcValid &= CWinDirStatModel::Get()->UserDefinedCleanupWorksForItem(&udc, item);
        }

        customMenu->AppendMenu(MF_STRING, ID_USERDEFINEDCLEANUP0 + iCurrent, string.c_str());
        SetMenuItem(customMenu, ID_USERDEFINEDCLEANUP0 + iCurrent, udcValid, true);
    }

    // conditionally disable menu if empty
    if (customMenu) SetMenuItem(menu,
        customMenuPos, customMenu->GetMenuItemCount() > 0 && scanReady);
}

void CMainFrame::OnAdvancedShadowCopy(const UINT nID)
{
    const WCHAR driveLetter = wds::strAlpha[nID - ID_TOOLS_SHADOW_COPY_BASE];
    const std::wstring drive = std::format(L"{:c}:", driveLetter);

    bool success = false;
    CProgressDlg dlg(0, CProgressDlg::Flags::NoCancel, this, [&](CProgressDlg*)
    {
        success = CreateShadowCopy(drive);
    });
    dlg.DoModal();

    if (!success)
    {
        const std::wstring msg = Localization::Format(IDS_SHADOW_COPY_FAILED, GetDrive(drive));
        WdsMessageBox(*this, msg, wds::strWinDirStat, MB_ICONERROR | MB_OK);
    }
}

void CMainFrame::OnAdvancedDefrag(const UINT nID)
{
    const WCHAR driveLetter = wds::strAlpha[nID - ID_TOOLS_DEFRAG_BASE];
    ExecuteCommandInConsole(std::format(L"DEFRAG.EXE {:c}: /O", driveLetter), L"DEFRAG");
}

void CMainFrame::OnAdvancedChkdsk(const UINT nID)
{
    const WCHAR driveLetter = wds::strAlpha[nID - ID_TOOLS_CHKDSK_BASE];
    ExecuteCommandInConsole(std::format(L"CHKDSK.EXE {:c}: /F", driveLetter), L"CHKDSK");
}

void CMainFrame::UpdateToolsMenu(CMenu* menu)
{
    // menu is the Tools popup menu itself
    // Find each operation submenu and populate with drives
    auto [shadowCopyMenu, shadowCopyPos] = LocateNamedMenu(menu, Localization::Lookup(IDS_MENU_SHADOW_COPY), true);
    auto [defragMenu, defragPos] = LocateNamedMenu(menu, Localization::Lookup(IDS_MENU_DEFRAGMENT), true);
    auto [chkdskMenu, chkdskPos] = LocateNamedMenu(menu, Localization::Lookup(IDS_MENU_CHKDSK), true);

    // Get available local drives and conditionally enable based on elevation
    const auto drives = GetDriveList({DRIVE_FIXED, DRIVE_REMOVABLE, DRIVE_RAMDISK});
    SetMenuItem(menu, shadowCopyPos, IsElevationActive() && !drives.empty());
    SetMenuItem(menu, defragPos, IsElevationPossible() && !drives.empty());
    SetMenuItem(menu, chkdskPos, IsElevationPossible() && !drives.empty());

    for (const auto& drive : drives)
    {
        // Get volume label for display
        const std::wstring volumeName = GetVolumeName(drive);
        const std::wstring displayName = volumeName.empty()
            ? GetDrive(drive) : std::format(L"{:.2} ({})", drive, volumeName);

        const int driveIndex = std::toupper(drive[0]) - L'A';
        shadowCopyMenu->AppendMenu(MF_STRING, ID_TOOLS_SHADOW_COPY_BASE + driveIndex, displayName.c_str());
        defragMenu->AppendMenu(MF_STRING, ID_TOOLS_DEFRAG_BASE + driveIndex, displayName.c_str());
        chkdskMenu->AppendMenu(MF_STRING, ID_TOOLS_CHKDSK_BASE + driveIndex, displayName.c_str());
    }
}

void CMainFrame::SetLogicalFocus(const LOGICAL_FOCUS lf)
{
    if (lf != m_logicalFocus)
    {
        m_logicalFocus = lf;
        UpdatePaneText();

        CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_SELECTION_STYLE);
    }
}

LOGICAL_FOCUS CMainFrame::GetLogicalFocus() const
{
    return m_logicalFocus;
}

void CMainFrame::MoveFocus(const LOGICAL_FOCUS logicalFocus)
{
    switch (logicalFocus)
    {
        case LF_EXTLIST: GetExtensionView()->SetFocus(); break;
        case LF_DUPELIST: GetFileDupeView()->SetFocus(); break;
        case LF_TOPLIST: GetFileTopView()->SetFocus(); break;
        case LF_SEARCHLIST: GetFileSearchView()->SetFocus(); break;
        case LF_WATCHERLIST: GetFileWatcherView()->SetFocus(); break;
        case LF_PERMSLIST: GetFilePermsView()->SetFocus(); break;
        case LF_FILETREE: GetFileTabbedView()->SetFocus(); break;
        case LF_NONE:
        {
            SetLogicalFocus(LF_NONE);
            m_wndDeadFocus.SetFocus();
        }
    }
}

void CMainFrame::UpdatePaneText()
{
    const auto focus = GetLogicalFocus();
    std::wstring fileSelectionText = !CWinDirStatModel::Get()->IsScanRunning() ?
        Localization::Lookup(IDS_IDLEMESSAGE) : wds::strEmpty;
    ULONGLONG size = MAXULONGLONG;

    // Allow override on hover (check active graph pane)
    if (const auto hoverInfo = GetActiveGraphPane()->GetHoverInfo(); !hoverInfo.path.empty())
    {
        fileSelectionText = hoverInfo.path;
        size = hoverInfo.size;
    }

    // Only get the data if the scan model is not actively updating
    else if (CWinDirStatModel::Get()->IsScanSettled())
    {
        if (focus != LF_EXTLIST)
        {
            const auto& items = CWinDirStatModel::Get()->GetAllSelected();
            if (items.size() == 1)
            {
                // If single item selected, show full path
                const auto path = items.front()->GetPath();
                if (!path.empty()) fileSelectionText = path;
            }
            else if (items.size() > 1)
            {
                // If multiple items are selected, show the statistics of selected items, files, and folders
                ULONGLONG totalFiles = 0;
                ULONGLONG totalFolders = 0;
                for (const auto& item : items)
                {
                    if (item->IsTypeOrFlag(IT_FILE)) totalFiles++;
                    if (item->IsTypeOrFlag(IT_DIRECTORY)) totalFolders++;
                    totalFiles += item->GetFilesCount();
                    totalFolders += item->GetFoldersCount();
                }
                fileSelectionText = Localization::Format(IDS_ITEMSs_SELECTED_FILESs_FOLDERSs,
                    FormatCount(items.size()), FormatCount(totalFiles), FormatCount(totalFolders));
            }

            for (size = 0; const auto& item : items)
            {
                size += COptions::TreeMapUseLogical ? item->GetSizeLogical() : item->GetSizePhysical();
            }

        }
        else if (fileSelectionText.empty())
        {
            fileSelectionText = wds::chrStar + CWinDirStatModel::Get()->GetHighlightExtension();
        }
    }

    // Update select physical size
    const CClientDC dc(this);
    SetStatusPaneText(dc, ID_STATUSPANE_IDLE_INDEX, fileSelectionText);
    SetStatusPaneText(dc, ID_STATUSPANE_SIZE_INDEX, (size == MAXULONGLONG) ? wds::strEmpty :
        std::format(L"{}: \u2211 {}", Localization::Lookup(COptions::TreeMapUseLogical ? IDS_COL_SIZE_LOGICAL : IDS_COL_SIZE_PHYSICAL), FormatBytes(size)), 175);
    SetStatusPaneText(dc, ID_STATUSPANE_RAM_INDEX, CDirStatApp::GetCurrentProcessMemoryInfo(), 175);

    // Scan mode (Normal vs Duplicates). Drawn by CWdsStatusBar as a drop-down
    // chip, so it sizes itself rather than going through SetStatusPaneText. The
    // chip sits left of the stretchy idle pane, so a width change moves that
    // pane - and with it the progress bar parented to the status bar.
    if (m_wndStatusBar.SetDropDownPaneText(Localization::Lookup(
        COptions::ScanForDuplicates ? IDS_SCAN_MODE_DUPLICATES : IDS_SCAN_MODE_NORMAL)))
    {
        LayoutStatusProgress();
    }
}

// Keeps the status bar's progress control aligned with the (stretchy) idle pane.
void CMainFrame::LayoutStatusProgress()
{
    if (!IsWindow(m_wndStatusBar.m_hWnd) || m_progress.m_hWnd == nullptr) return;

    CRect rc;
    m_wndStatusBar.GetItemRect(ID_STATUSPANE_IDLE_INDEX, rc);
    rc.DeflateRect(DpiRest(3, &m_wndStatusBar), DpiRest(4, &m_wndStatusBar),
        DpiRest(5, &m_wndStatusBar), DpiRest(4, &m_wndStatusBar));
    m_progress.MoveWindow(rc);
}

/////////////////////////////////////////////////////////////////////////////
// CWdsStatusBar - the scan-mode pane, drawn as a clickable drop-down chip.

namespace
{
    // Perceived brightness (ITU-R BT.601). Used only to decide which direction
    // "more contrast than the background" is, so the chip stands out on a light
    // status bar and on a dark one without hard-coding either palette.
    bool IsDarkColor(const COLORREF color) noexcept
    {
        return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000 < 128;
    }

    COLORREF ShiftColor(const COLORREF color, const int amount) noexcept
    {
        const auto channel = [amount](const int value) noexcept
        {
            return static_cast<BYTE>(std::clamp(value + amount, 0, 255));
        };
        return RGB(channel(GetRValue(color)), channel(GetGValue(color)), channel(GetBValue(color)));
    }

    // Chip metrics, in unscaled pixels (run through DpiRest before use).
    constexpr int ChipInsetX = 3;   // Gap between the pane edge and the chip
    constexpr int ChipInsetY = 2;   // Gap above and below the chip
    constexpr int ChipRadius = 4;   // Corner rounding
    constexpr int ChipPadX = 8;     // Padding between the chip frame and its content
    constexpr int ChevronGap = 6;   // Gap between the label and the chevron
    constexpr int ChevronHalfW = 4; // Half the chevron's width
    constexpr int ChevronH = 3;     // Chevron height
    constexpr int ChipSlack = 6;    // GetTextExtent can under-measure what DrawText needs
}

BEGIN_MESSAGE_MAP(CWdsStatusBar, CMFCStatusBar)
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_SETCURSOR()
    ON_MESSAGE(WM_MOUSELEAVE, &CWdsStatusBar::OnMouseLeave)
END_MESSAGE_MAP()

int CWdsStatusBar::DropDownPaneIndex() const
{
    return m_dropDownPaneId == 0 ? -1 : CommandToIndex(m_dropDownPaneId);
}

bool CWdsStatusBar::HitsDropDownPane(const CPoint point) const
{
    const int index = DropDownPaneIndex();
    if (index < 0) return false;

    CRect rect;
    GetItemRect(index, rect);
    return rect.PtInRect(point) != FALSE;
}

int CWdsStatusBar::MeasureTextWidth(const std::wstring& text) const
{
    // The status bar has its own font; measuring with the frame's DC (which is
    // what the ordinary panes do) undersizes the pane whenever the two differ.
    CClientDC dc(const_cast<CWdsStatusBar*>(this));
    const HGDIOBJ oldFont = dc.SelectObject(GetCurrentFont());
    const int width = dc.GetTextExtent(text.c_str(), static_cast<int>(text.size())).cx;
    dc.SelectObject(oldFont);
    return width;
}

bool CWdsStatusBar::SetDropDownPaneText(const std::wstring& text)
{
    const int index = DropDownPaneIndex();
    if (index < 0) return false;

    // The width is recomputed on every call rather than cached against the label,
    // because it also depends on the bar's DPI - and the DPI is not yet final when
    // the first label is set from CMainFrame::OnCreate (the frame has not been
    // positioned on its monitor yet, so GetDpiForWindow still answers 96). Caching
    // on the label alone froze a 96-DPI width into a 144-DPI bar and the label came
    // out ellipsised. Recomputing also covers the window being dragged between
    // monitors of different scaling.
    const int width = MeasureTextWidth(text) +
        DpiRest(2 * (ChipInsetX + ChipPadX) + ChevronGap + 2 * ChevronHalfW + ChipSlack, this);

    // UpdatePaneText runs off the display timer, so this is reached several times a
    // second; only touch the bar when something actually moved.
    if (width == m_dropDownWidth && GetPaneText(index) == text.c_str()) return false;

    m_dropDownWidth = width;
    SetPaneWidth(index, width);
    SetPaneText(index, text.c_str());
    return true;
}

COLORREF CWdsStatusBar::GetOrdinaryPaneTextColor() const
{
    // Ask the visual manager the same question it is asked while painting a
    // normal pane, passing a real neighbouring pane, so the chip's label always
    // comes out the identical shade to the text beside it - whatever the visual
    // manager and theme decide, in light mode and in dark.
    //
    // Panes MFC considers disabled (SBPS_DISABLED, which it applies to any pane
    // whose indicator ID has no command handler) are painted in a washed-out
    // grey. Prefer a pane that is not disabled so the chip - which is very much
    // enabled, being the one thing on this bar you can click - never renders in
    // the greyed shade while its neighbours render in the normal one.
    auto paneColor = [this](CMFCStatusBarPaneInfo* pane)
    {
        return CMFCVisualManager::GetInstance()->GetStatusBarPaneTextColor(
            const_cast<CWdsStatusBar*>(this), pane);
    };

    CMFCStatusBarPaneInfo* fallback = nullptr;
    for (const int i : std::views::iota(0, GetCount()))
    {
        CMFCStatusBarPaneInfo* pane = _GetPanePtr(i);
        if (pane == nullptr || pane->nID == m_dropDownPaneId) continue;
        if ((pane->nStyle & SBPS_DISABLED) == 0) return paneColor(pane);
        if (fallback == nullptr) fallback = pane;
    }

    return fallback != nullptr ? paneColor(fallback) : GetGlobalData()->clrBtnText;
}

void CWdsStatusBar::OnDrawPane(CDC* pDC, CMFCStatusBarPaneInfo* pPane)
{
    if (pPane == nullptr || m_dropDownPaneId == 0 || pPane->nID != m_dropDownPaneId)
    {
        CMFCStatusBar::OnDrawPane(pDC, pPane);
        return;
    }

    DrawDropDownPane(pDC, pPane);
}

void CWdsStatusBar::DrawDropDownPane(CDC* pDC, const CMFCStatusBarPaneInfo* pPane) const
{
    ASSERT_VALID(pDC);

    const CRect rectPane(pPane->rect);
    if (rectPane.IsRectEmpty() || !pDC->RectVisible(rectPane)) return;

    // The same explicit background fill the stock pane painter performs.
    if (pPane->clrBackground != static_cast<COLORREF>(-1))
    {
        pDC->FillSolidRect(rectPane, pPane->clrBackground);
    }

    // Shade the chip relative to whatever the bar was actually painted with:
    // DoPaint draws the bar background into this DC before the panes, so reading
    // it back beats guessing at a palette the visual manager owns.
    COLORREF back = pPane->clrBackground;
    if (back == static_cast<COLORREF>(-1))
    {
        back = pDC->GetPixel(rectPane.left + 1, rectPane.CenterPoint().y);
        if (back == CLR_INVALID) back = GetGlobalData()->clrBarFace;
    }

    const int direction = IsDarkColor(back) ? 1 : -1;
    const COLORREF frame = ShiftColor(back, direction * (m_dropDownHot || m_dropDownOpen ? 78 : 48));
    const COLORREF fill = ShiftColor(back, direction *
        (m_dropDownOpen ? 42 : m_dropDownHot ? 26 : 10));

    // The resting label colour matches the neighbouring panes exactly; hover and
    // pressed lift it for the extra contrast the highlighted fill wants.
    COLORREF label = GetOrdinaryPaneTextColor();
    if (m_dropDownHot || m_dropDownOpen) label = ShiftColor(label, direction * 60);

    CRect chip(rectPane);
    chip.DeflateRect(DpiRest(ChipInsetX, this), DpiRest(ChipInsetY, this));
    if (chip.IsRectEmpty()) return;

    CBrush chipBrush(fill);
    CPen chipPen(PS_SOLID, 1, frame);
    CBrush* const oldBrush = pDC->SelectObject(&chipBrush);
    CPen* const oldPen = pDC->SelectObject(&chipPen);
    const int radius = DpiRest(ChipRadius, this);
    pDC->RoundRect(chip, CPoint(radius, radius));
    pDC->SelectObject(oldBrush);
    pDC->SelectObject(oldPen);

    // Chevron on the right; the label gets the space that is left.
    const int chevronHalfWidth = DpiRest(ChevronHalfW, this);
    const int chevronHeight = DpiRest(ChevronH, this);
    const int chevronCenterX = chip.right - DpiRest(ChipPadX, this) - chevronHalfWidth;
    const int chevronCenterY = chip.CenterPoint().y;

    CRect rectText(chip);
    rectText.left += DpiRest(ChipPadX, this);
    rectText.right = chevronCenterX - chevronHalfWidth - DpiRest(ChevronGap, this);

    if (pPane->lpszText != nullptr && !rectText.IsRectEmpty())
    {
        const COLORREF oldColor = pDC->SetTextColor(label);
        pDC->DrawText(pPane->lpszText, static_cast<int>(wcslen(pPane->lpszText)), rectText,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        pDC->SetTextColor(oldColor);
    }

    POINT chevron[3]
    {
        { chevronCenterX - chevronHalfWidth, chevronCenterY - chevronHeight / 2 },
        { chevronCenterX + chevronHalfWidth, chevronCenterY - chevronHeight / 2 },
        { chevronCenterX, chevronCenterY + chevronHeight - chevronHeight / 2 }
    };

    CBrush arrowBrush(label);
    CPen arrowPen(PS_SOLID, 1, label);
    CBrush* const oldArrowBrush = pDC->SelectObject(&arrowBrush);
    CPen* const oldArrowPen = pDC->SelectObject(&arrowPen);
    pDC->Polygon(chevron, static_cast<int>(std::ssize(chevron)));
    pDC->SelectObject(oldArrowBrush);
    pDC->SelectObject(oldArrowPen);
}

void CWdsStatusBar::SetDropDownHot(const bool hot)
{
    if (m_dropDownHot == hot) return;

    m_dropDownHot = hot;
    if (const int index = DropDownPaneIndex(); index >= 0) InvalidatePaneContent(index);
}

void CWdsStatusBar::OnLButtonDown(const UINT nFlags, const CPoint point)
{
    if (!HitsDropDownPane(point))
    {
        CMFCStatusBar::OnLButtonDown(nFlags, point);
        return;
    }

    const int index = DropDownPaneIndex();
    CRect rect;
    GetItemRect(index, rect);

    // Keep the chip drawn pressed for as long as its menu is on screen.
    m_dropDownOpen = true;
    InvalidatePaneContent(index);
    UpdateWindow();

    CMainFrame::Get()->ShowScanModeMenu(this, rect);

    m_dropDownOpen = false;

    // The pointer is usually elsewhere once the menu closes, and a click that
    // never moved it produces no WM_MOUSEMOVE, so recheck the hover state.
    CPoint cursor;
    GetCursorPos(&cursor);
    ScreenToClient(&cursor);
    m_dropDownHot = HitsDropDownPane(cursor);

    // Always repaint, even when the mode changed and the label already forced a
    // relayout: that repaint ran from inside the menu loop, while the chip was
    // still flagged open, so it painted the pressed shade. Without this the chip
    // stays looking pressed until something else happens to invalidate it.
    InvalidatePaneContent(index);
}

void CWdsStatusBar::OnMouseMove(const UINT nFlags, const CPoint point)
{
    SetDropDownHot(HitsDropDownPane(point));

    if (m_dropDownHot && !m_trackingMouse)
    {
        TRACKMOUSEEVENT track{ .cbSize = sizeof(TRACKMOUSEEVENT), .dwFlags = TME_LEAVE, .hwndTrack = m_hWnd };
        m_trackingMouse = TrackMouseEvent(&track) != FALSE;
    }

    CMFCStatusBar::OnMouseMove(nFlags, point);
}

LRESULT CWdsStatusBar::OnMouseLeave(WPARAM, LPARAM)
{
    m_trackingMouse = false;
    SetDropDownHot(false);
    return 0;
}

BOOL CWdsStatusBar::OnSetCursor(CWnd* pWnd, const UINT nHitTest, const UINT message)
{
    CPoint cursor;
    if (GetCursorPos(&cursor))
    {
        ScreenToClient(&cursor);
        if (HitsDropDownPane(cursor))
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
    }

    return CMFCStatusBar::OnSetCursor(pWnd, nHitTest, message);
}

/////////////////////////////////////////////////////////////////////////////

// Pops a small menu anchored to the mode chip so the user can see both scan
// modes (and which one is active) rather than having to guess that the pane
// toggles when clicked. Returns whether the mode actually changed.
bool CMainFrame::ShowScanModeMenu(CWnd* anchor, const CRect& paneRect)
{
    constexpr UINT ID_MODE_NORMAL = 1;
    constexpr UINT ID_MODE_DUPLICATES = 2;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_MODE_NORMAL,
        Localization::Lookup(IDS_SCAN_MODE_MENU_NORMAL).c_str());
    menu.AppendMenu(MF_STRING, ID_MODE_DUPLICATES,
        Localization::Lookup(IDS_SCAN_MODE_MENU_DUPLICATES).c_str());
    menu.CheckMenuRadioItem(ID_MODE_NORMAL, ID_MODE_DUPLICATES,
        COptions::ScanForDuplicates ? ID_MODE_DUPLICATES : ID_MODE_NORMAL, MF_BYCOMMAND);

    // Anchor to the top-left of the pane; the menu opens upward over the bar.
    CRect screenRect = paneRect;
    anchor->ClientToScreen(&screenRect);
    const UINT cmd = menu.TrackPopupMenu(
        TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        screenRect.left, screenRect.top, this);

    if (cmd != ID_MODE_NORMAL && cmd != ID_MODE_DUPLICATES) return false;
    return SetScanMode(cmd == ID_MODE_DUPLICATES);
}

void CMainFrame::ToggleScanMode()
{
    SetScanMode(!COptions::ScanForDuplicates);
}

// Switches between the plain size scan and the duplicate-detection scan.
// Returns whether the mode actually changed.
bool CMainFrame::SetScanMode(const bool scanForDuplicates)
{
    // Ignore no-op selections so a scan is never needlessly disturbed.
    if (scanForDuplicates == COptions::ScanForDuplicates) return false;

    CWinDirStatModel* model = CWinDirStatModel::Get();
    const bool hasResults = model->GetRootItem() != nullptr;

    // Turning duplicate detection ON needs every file hashed, and hashes are only
    // produced while scanning - so the scan has to run again from the start. That
    // throws away the current results and can take a long while on a big volume,
    // so confirm it rather than acting on a stray click.
    if (scanForDuplicates && hasResults &&
        CMessageBoxDlg::Show(Localization::Lookup(IDS_SCAN_MODE_RESCAN_PROMPT),
            MB_YESNO | MB_ICONQUESTION, this) != IDYES)
    {
        return false;
    }

    COptions::ScanForDuplicates = scanForDuplicates;
    UpdatePaneText();

    if (!scanForDuplicates)
    {
        // Turning it OFF needs no rescan: everything the other views show has
        // already been collected, and duplicate detection simply stops feeding
        // the Duplicates tab. Retire the tab and leave the scan alone.
        //
        // The accumulated duplicate state is deliberately *not* cleared here:
        // scanning threads call CFileDupeControl::ProcessDuplicate, so tearing
        // its trackers down from the UI thread mid-scan would race with them.
        // The next scan's ClearScanState does that safely once the workers idle.
        GetFileTabbedView()->SetDupeTabVisibility(false);
        return true;
    }

    if (hasResults)
    {
        // StartScan publishes MODEL_CHANGE_NEW_ROOT, which is what makes the
        // Duplicates tab appear, so no explicit tab work is needed here.
        model->StartScan(model->GetScanPathSpec());
    }

    return true;
}

void CMainFrame::OnUpdateEnableControl(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(true);
}

void CMainFrame::OnSize(const UINT nType, const int cx, const int cy)
{
    CFrameWndEx::OnSize(nType, cx, cy);
    LayoutStatusProgress();
}

/////////////////////////////////////////////////////////////////////////////

void CMainFrame::OnUpdateViewShowTreeMap(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!CWinDirStatModel::Get()->IsScanRunning());
    SetNativeMenuRadio(pCmdUI, !COptions::UseFlameGraph);
}

void CMainFrame::OnUpdateTreeMapUseLogical(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!CWinDirStatModel::Get()->IsScanRunning());
    SetNativeMenuRadio(pCmdUI, COptions::TreeMapUseLogical);
}

void CMainFrame::OnUpdateTreeMapUsePhysical(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!CWinDirStatModel::Get()->IsScanRunning());
    SetNativeMenuRadio(pCmdUI, !COptions::TreeMapUseLogical);
}

void CMainFrame::OnUpdateViewShowFileTypes(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetExtensionView()->IsShowTypes());
}

void CMainFrame::OnUpdateViewGroupUnregisteredTypes(CCmdUI* pCmdUI)
{
    const CWinDirStatModel* model = CWinDirStatModel::Get();
    pCmdUI->Enable(GetExtensionView()->IsShowTypes() && model->IsScanSettled());
    pCmdUI->SetCheck(COptions::GroupUnregisteredTypes);
}

void CMainFrame::OnUpdateViewShowWatcher(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetFileTabbedView()->IsWatcherTabVisible());
}

void CMainFrame::SelectGraphPane(const bool useFlameGraph)
{
    // Accelerators still dispatch WM_COMMAND while the corresponding menu
    // item is disabled. Do not switch away from the pane suspended for a scan.
    if (CWinDirStatModel::Get()->IsScanRunning()) return;
    if (COptions::UseFlameGraph == useFlameGraph && IsActiveGraphPaneShown()) return;

    COptions::UseFlameGraph = useFlameGraph;
    ShowActiveGraphPane(true);
    RebuildLayout();
}

void CMainFrame::OnViewTreeMap()
{
    SelectGraphPane(false);
}

void CMainFrame::OnViewFlameGraph()
{
    SelectGraphPane(true);
}

void CMainFrame::OnUpdateViewFlameGraph(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!CWinDirStatModel::Get()->IsScanRunning());
    SetNativeMenuRadio(pCmdUI, COptions::UseFlameGraph);
}

static void SortItemRecursive(CItem* item)
{
    if (item == nullptr || item->IsLeaf()) return;
    COptions::TreeMapUseLogical ? item->SortItemsBySizeLogical() : item->SortItemsBySizePhysical();
    for (CItem* child : item->GetChildren())
    {
        SortItemRecursive(child);
    }
}

void CMainFrame::OnViewTreeMapUseLogical()
{
    if (!COptions::TreeMapUseLogical)
    {
        COptions::TreeMapUseLogical = true;
        CItem* root = CWinDirStatModel::Get()->GetRootItem();
        if (root)
        {
            SortItemRecursive(root);
            CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_SIZE_MODE);
        }
        UpdatePaneText();
    }
}

void CMainFrame::OnViewTreeMapUsePhysical()
{
    if (COptions::TreeMapUseLogical)
    {
        COptions::TreeMapUseLogical = false;
        CItem* root = CWinDirStatModel::Get()->GetRootItem();
        if (root)
        {
            SortItemRecursive(root);
            CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_SIZE_MODE);
        }
        UpdatePaneText();
    }
}

void CMainFrame::OnViewShowFileTypes()
{
    GetExtensionView()->ShowTypes(!GetExtensionView()->IsShowTypes());
    if (GetExtensionView()->IsShowTypes())
    {
        RestoreExtensionView();
    }
    else
    {
        MinimizeExtensionView();
    }
}

void CMainFrame::OnViewGroupUnregisteredTypes()
{
    COptions::GroupUnregisteredTypes = !COptions::GroupUnregisteredTypes;

    // Recolor extensions so the unregistered group shares one color, then refresh the list and graph
    CWinDirStatModel::Get()->RebuildExtensionData();
    GetExtensionView()->OnUpdate(nullptr, MODEL_CHANGE_NONE, nullptr);
    CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_TREEMAP_STYLE);
}

void CMainFrame::OnViewShowExtensionsOnTreeMap()
{
    if (COptions::UseFlameGraph) return;

    COptions::TreeMapShowExtensions = !static_cast<bool>(COptions::TreeMapShowExtensions);
    COptions::TreeMapOptions.showExtensions = COptions::TreeMapShowExtensions;
    CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_TREEMAP_STYLE);
}

void CMainFrame::OnUpdateViewShowExtensionsOnTreeMap(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!COptions::UseFlameGraph && !CWinDirStatModel::Get()->IsScanRunning());
    pCmdUI->SetCheck(COptions::TreeMapOptions.showExtensions);
}

void CMainFrame::OnViewShowFolderFramesOnTreeMap()
{
    if (COptions::UseFlameGraph) return;

    COptions::TreeMapShowFolderFrames = !static_cast<bool>(COptions::TreeMapShowFolderFrames);
    COptions::TreeMapOptions.showFolderFrames = COptions::TreeMapShowFolderFrames;
    CWinDirStatModel::Get()->NotifyPanes(MODEL_CHANGE_TREEMAP_STYLE);
}

void CMainFrame::OnUpdateViewShowFolderFramesOnTreeMap(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!COptions::UseFlameGraph && !CWinDirStatModel::Get()->IsScanRunning());
    pCmdUI->SetCheck(COptions::TreeMapOptions.showFolderFrames);
}

//
// CToolBarLabel. A non-interactive, text-only toolbar entry used to
// caption the contextual watcher button group.
//
class CToolBarLabel final : public CMFCToolBarButton
{
    DECLARE_DYNCREATE(CToolBarLabel)

public:
    CToolBarLabel() = default;
    CToolBarLabel(const UINT id, const std::wstring& text)
        : CMFCToolBarButton(id, -1, text.c_str())
    {
        m_bText = TRUE;
        m_bImage = FALSE;
        m_nStyle = TBBS_DISABLED;
    }

    SIZE OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL /*bHorz*/) override
    {
        // Pad the measured text by its height to keep the caption visually separated
        CFont* oldFont = pDC->SelectObject(&GetGlobalData()->fontRegular);
        const CSize textSize = pDC->GetTextExtent(m_strText);
        pDC->SelectObject(oldFont);
        return { textSize.cx + textSize.cy, sizeDefault.cy };
    }

    void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* /*pImages*/, BOOL /*bHorz*/,
        BOOL /*bCustomizeMode*/, BOOL /*bHighlight*/, BOOL /*bDrawBorder*/, BOOL /*bGrayDisabledButtons*/) override
    {
        CRect textRect(rect);
        CFont* oldFont = pDC->SelectObject(&GetGlobalData()->fontRegular);
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(Icons::NeutralRef());
        pDC->DrawText(m_strText, textRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        pDC->SelectObject(oldFont);
    }
};

IMPLEMENT_DYNCREATE(CToolBarLabel, CMFCToolBarButton)

static void PaintWatcherAutoScroll(Gdiplus::Graphics& g)
{
    const bool enabled = COptions::WatcherAutoScroll;
    Icons::PaintCharacter(g, L'⤓', enabled ? RGB(0, 156, 221) : Icons::NeutralRef());
    if (!enabled)
    {
        Gdiplus::Pen slash(Icons::C(204, 0, 0), 6);
        slash.SetStartCap(Gdiplus::LineCapRound);
        slash.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&slash, 12, 52, 52, 12);
    }
}

void CMainFrame::RebuildToolBar()
{
    // In ribbon mode the classic toolbar is never created; nothing to rebuild.
    if (m_wndToolBar.GetSafeHwnd() == nullptr) return;

    const auto imageSize = COptions::LargeToolBar ? 32 : 20;
    const auto scale = COptions::LargeToolBar ? (32.0f / 20.0f) : 1.0f;

    // Remove all existing buttons
    if (CDirStatApp::Get()->m_pMainWnd == nullptr) return;
    while (m_wndToolBar.GetCount() > 0)
        m_wndToolBar.RemoveButton(0);

    // Clear the shared image list and resize buttons to match the new icon size
    CMFCToolBar::GetImages()->Clear();
    CMFCToolBar::SetSizes(
        { static_cast<LONG>(m_defaultButtonSize.cx * scale),
          static_cast<LONG>(m_defaultButtonSize.cy * scale)},
        { imageSize, imageSize });

    using Painter = std::function<void(Gdiplus::Graphics&)>;
    static const std::vector<std::tuple<UINT, std::wstring_view, Painter>> toolbarButtons =
    {
        { ID_FILE_SELECT,             IDS_FILE_SELECT,             Icons::PaintFileSelect},
        { ID_SEPARATOR,               {},{}},
        { ID_SCAN_RESUME,             IDS_RESUME,                  Icons::Char(L'▶', RGB( 50, 205,  50))},
        { ID_SCAN_SUSPEND,            IDS_SUSPEND,                 Icons::PaintPause},
        { ID_SCAN_STOP,               IDS_STOP,                    Icons::Char(L'■', RGB(220,  20,  60))},
        { ID_SEPARATOR,               {},{}},
        { ID_REFRESH_ALL,             IDS_REFRESH_ALL,             Icons::Char(L'↻', RGB(  0, 156, 221))},
        { ID_REFRESH_SELECTED,        IDS_REFRESH_SELECTED,        Icons::PaintRefreshSelected},
        { ID_SEPARATOR,               {},{}},
        { ID_SEARCH,                  IDS_SEARCH_TITLE,            Icons::Char(L'⌕', Icons::NeutralRef())},
        { ID_FILTER,                  IDS_PAGE_FILTERING_TITLE,    [](auto& g){ Icons::PaintFilter(g, CFiltering::IsFilterActive()); } },
        { ID_SEPARATOR,               {},{}},
        { ID_CLEANUP_OPEN_SELECTED,   IDS_CLEANUP_OPEN_SELECTED,   Icons::PaintOpenSelected},
        { ID_CLEANUP_EXPLORER_SELECT, IDS_CLEANUP_EXPLORER_SELECT, Icons::PaintExplorerSelect},
        { ID_EDIT_COPY_CLIPBOARD,     IDS_EDIT_COPY_CLIPBOARD,     Icons::PaintEditCopyClipboard},
        { ID_CLEANUP_OPEN_IN_CONSOLE, IDS_CLEANUP_OPEN_IN_CONSOLE, Icons::PaintOpenInConsole},
        { ID_CLEANUP_PROPERTIES,      IDS_CLEANUP_PROPERTIES,      Icons::PaintProperties},
        { ID_SEPARATOR,               {},{}},
        { ID_CLEANUP_DELETE_BIN,      IDS_CLEANUP_DELETE_BIN,      Icons::PaintDeleteBin},
        { ID_CLEANUP_DELETE,          IDS_CLEANUP_DELETE,          Icons::PaintDelete},
        { ID_SEPARATOR,               {},{}},
        { ID_TREEMAP_ZOOMIN,          IDS_TREEMAP_ZOOMIN,          [](auto& g) { Icons::PaintMagnifier(g, true);}},
        { ID_TREEMAP_ZOOMOUT,         IDS_TREEMAP_ZOOMOUT,         [](auto& g){ Icons::PaintMagnifier(g, false);}},
        { ID_SEPARATOR,               {},{}},
        { ID_VIEW_WINDOW_LAYOUT,      IDS_WINDOW_LAYOUT,           Icons::PaintWindowLayout},
        { ID_SEPARATOR,               {},{}},
        { ID_CONFIGURE,               IDS_MENU_SETTINGS,           Icons::PaintGear},
        { ID_HELP_MANUAL,             IDS_HELP_MANUAL,             Icons::PaintHelp},
        { ID_SEPARATOR,               {},{}},
        { ID_WATCHER_LABEL,           IDS_WATCHER,                 {}},
        { ID_WATCHER_START,           {},                          Icons::Char(L'▶', RGB( 50, 205,  50))},
        { ID_WATCHER_PAUSE,           {},                          Icons::PaintPause},
        { ID_WATCHER_AUTOSCROLL,      {},                          PaintWatcherAutoScroll},
        { ID_WATCHER_CLEAR,           {},                          Icons::PaintDelete},
    };

    for (const auto& [id, text, painter] : toolbarButtons)
    {
        if (id == ID_SEPARATOR)
        {
            m_wndToolBar.InsertSeparator();
            continue;
        }

        if (id == ID_WATCHER_LABEL)
        {
            m_wndToolBar.InsertButton(CToolBarLabel(id, Localization::Lookup(text) + L":"));
            continue;
        }

        int index = 0;
        if (painter)
        {
            CBitmap bitmap;
            bitmap.Attach(Icons::MakeBitmap(imageSize, painter));
            index = CMFCToolBar::GetImages()->AddImage(bitmap, TRUE);
        }

        CMFCToolBarButton button(id, index, nullptr, TRUE, TRUE);
        button.m_bText = FALSE;
        button.m_nStyle = TBBS_DISABLED;
        if (!text.empty()) button.m_strText = Localization::Lookup(text).c_str();
        m_wndToolBar.InsertButton(button);
    }

    // The watcher buttons are contextual and only shown while its tab is active
    SetWatcherToolBarButtons(m_fileTabbedView != nullptr &&
        m_fileTabbedView->IsFileWatcherViewTabActive());

    m_wndToolBar.AdjustLayout();
}

void CMainFrame::SetWatcherToolBarButtons(const bool visible)
{
    if (m_wndToolBar.GetSafeHwnd() == nullptr) return;

    // The group spans the separator before the caption label through the last button
    const int labelIndex = m_wndToolBar.CommandToIndex(ID_WATCHER_LABEL);
    if (labelIndex < 1) return;

    bool changed = false;
    for (const int index : std::views::iota(labelIndex - 1, labelIndex + 5))
    {
        CMFCToolBarButton* button = m_wndToolBar.GetButton(index);
        if (button == nullptr || (button->IsVisible() != FALSE) == visible) continue;
        button->SetVisible(visible);
        changed = true;
    }

    // Recompute button locations and repaint; a size-only adjustment does
    // not refresh the layout when the docked toolbar extents are unchanged
    if (changed) m_wndToolBar.AdjustLayout();
}

void CMainFrame::OnWatcherStart()
{
    CFileWatcherControl::Get()->StartMonitoring();
}

void CMainFrame::OnUpdateWatcherStart(CCmdUI* pCmdUI)
{
    const auto* watcher = CFileWatcherControl::Get();
    pCmdUI->Enable(watcher != nullptr && !watcher->IsMonitoring() &&
        CWinDirStatModel::Get()->HasRootItem());
}

void CMainFrame::OnWatcherPause()
{
    CFileWatcherControl::Get()->StopMonitoring();
}

void CMainFrame::OnUpdateWatcherPause(CCmdUI* pCmdUI)
{
    const auto* watcher = CFileWatcherControl::Get();
    pCmdUI->Enable(watcher != nullptr && watcher->IsMonitoring());
}

void CMainFrame::OnWatcherAutoScroll()
{
    COptions::WatcherAutoScroll = !COptions::WatcherAutoScroll;
    RebuildToolBar();
}

void CMainFrame::OnUpdateWatcherAutoScroll(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(TRUE);
    pCmdUI->SetCheck(FALSE);
}

void CMainFrame::OnWatcherClear()
{
    CFileWatcherControl::Get()->ClearResults();
}

void CMainFrame::OnUpdateWatcherClear(CCmdUI* pCmdUI)
{
    const auto* watcher = CFileWatcherControl::Get();
    pCmdUI->Enable(watcher != nullptr && watcher->GetItemCount() > 0);
}

void CMainFrame::OnViewLargeToolBar()
{
    COptions::LargeToolBar = !COptions::LargeToolBar;
    RebuildToolBar();
}

void CMainFrame::OnUpdateViewLargeToolBar(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(COptions::LargeToolBar);
    pCmdUI->Enable((m_wndToolBar.GetStyle() & WS_VISIBLE) != 0);
}

void CMainFrame::OnViewRibbon()
{
    // The ribbon replaces the menu/toolbar and is built during frame creation,
    // so switching requires a restart. Persist first so the setting survives the
    // relaunch, then restart (RestartApplication saves remaining state via WM_CLOSE).
    COptions::UseRibbon = !COptions::UseRibbon;
    PersistedSetting::WritePersistedProperties();
    CDirStatApp::Get()->RestartApplication();
}

void CMainFrame::OnUpdateViewRibbon(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(COptions::UseRibbon);
}

void CMainFrame::OnConfigure()
{
    const bool restart = COptionsPropertySheet::ShowSettings();

    // Rebuild the toolbar so icons (e.g. the filter indicator) reflect the new settings
    RebuildToolBar();

    // Save settings in case the application exits abnormally
    PersistedSetting::WritePersistedProperties();

    if (restart)
    {
        CDirStatApp::Get()->RestartApplication();
    }
}

void CMainFrame::OnSysColorChange()
{
    GetFileTreeView()->SysColorChanged();
    GetExtensionView()->SysColorChanged();
    DrawTextCache::Get().ClearCache();

    // Redraw menus for dark mode
    DarkMode::SetAppDarkMode();
    RedrawWindow();
}

UINT CMainFrame::OnPowerBroadcast(UINT, LPARAM)
{
    OnSysColorChange();
    return TRUE;
}

LRESULT CMainFrame::OnUahDrawMenu(WPARAM wParam, LPARAM lParam)
{
    return DarkMode::HandleMenuMessage(GetCurrentMessage()->message, wParam, lParam, *this);
}

void CMainFrame::OnNcPaint()
{
    // Update the bottom of the menu bar that is not properly painted
    CFrameWndEx::OnNcPaint();
    DarkMode::DrawMenuClientArea(*this);
}

BOOL CMainFrame::OnNcActivate(BOOL bActive)
{
    // Update the bottom of the menu bar that is not properly painted
    const auto ret = CFrameWndEx::OnNcActivate(bActive);
    DarkMode::DrawMenuClientArea(*this);
    return ret;
}

BOOL CMainFrame::LoadFrame(const UINT nIDResource, const DWORD dwDefaultStyle, CWnd* pParentWnd, CCreateContext* pContext)
{
    if (!CFrameWndEx::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext))
    {
        return FALSE;
    }

    // With the ribbon enabled the classic menu bar is removed (SetMenu(nullptr)),
    // so only localize the menu when one actually exists.
    if (CMenu* menu = GetMenu(); menu != nullptr && menu->GetSafeHmenu() != nullptr)
    {
        Localization::UpdateMenu(*menu);
    }
    Localization::UpdateDialogs(*this);
    SetTitle(wds::strWinDirStat);

    return TRUE;
}

void CMainFrame::OnToolsWatcher()
{
    const bool visible = !GetFileTabbedView()->IsWatcherTabVisible();
    GetFileTabbedView()->SetWatcherTabVisibility(visible);
    if (visible)
    {
        GetFileTabbedView()->SetActiveWatcherView();
    }
}

void CMainFrame::OnToolsPermissions()
{
    GetFileTabbedView()->SetPermsTabVisibility(!GetFileTabbedView()->IsPermsTabVisible());

    // Re-check visibility: a cancelled scan leaves the tab hidden, so don't activate it
    if (GetFileTabbedView()->IsPermsTabVisible())
    {
        GetFileTabbedView()->SetActivePermsView();
    }
}

void CMainFrame::OnUpdateToolsPermissions(CCmdUI* pCmdUI)
{
    // Only allow launching a scan once the file tree has been fully populated
    const auto* model = CWinDirStatModel::Get();
    pCmdUI->SetCheck(GetFileTabbedView()->IsPermsTabVisible());
    pCmdUI->Enable(GetFileTabbedView()->IsPermsTabVisible() || model->IsScanSettled());
}

void CMainFrame::OnToolsStorageAnalytics()
{
    GetFileTabbedView()->SetStorageAnalyticsTabVisibility(!GetFileTabbedView()->IsStorageAnalyticsTabVisible());

    if (GetFileTabbedView()->IsStorageAnalyticsTabVisible())
    {
        GetFileTabbedView()->SetActiveStorageAnalyticsView();
    }
}

void CMainFrame::OnUpdateToolsStorageAnalytics(CCmdUI* pCmdUI)
{
    const auto* model = CWinDirStatModel::Get();
    pCmdUI->SetCheck(GetFileTabbedView()->IsStorageAnalyticsTabVisible());
    pCmdUI->Enable(GetFileTabbedView()->IsStorageAnalyticsTabVisible() || model->IsScanSettled());
}

void CMainFrame::OnViewWindowLayout()
{
    const int idx = m_wndToolBar.CommandToIndex(ID_VIEW_WINDOW_LAYOUT);
    CRect btnRect;
    m_wndToolBar.GetItemRect(idx, &btnRect);
    m_wndToolBar.ClientToScreen(&btnRect);
    m_layoutPopup.ShowAtButton(btnRect);
}

void CMainFrame::ConfigureSplitterCallbacks(int topo, int perm)
{
    m_splitter.ClearPaneTracking();
    m_subSplitter.ClearPaneTracking();

    auto showGraph = [this](bool visible) { ShowActiveGraphPane(visible); };
    auto showFileTypes = [this](bool visible) { GetExtensionView()->ShowTypes(visible); };

    switch (topo)
    {
    case LT_ROWS_SUB_COLS:
        m_splitter.TrackPane(perm == 0 ? 1 : 0, showGraph,
            [this, perm]() { m_splitter.SetSplitterPos(perm == 0 ? 1.0 : 0.0); });
        m_subSplitter.TrackPane(1, showFileTypes,
            [this]() { m_subSplitter.SetSplitterPos(1.0); });
        break;

    case LT_COLS_THREE:
        switch (perm)
        {
        case 0: // [FTV|graph] | ExtV
            m_splitter.TrackPane(1, showFileTypes,
                [this]() { m_splitter.SetSplitterPos(1.0); });
            m_subSplitter.TrackPane(1, showGraph,
                [this]() { m_subSplitter.SetSplitterPos(1.0); });
            break;
        case 1: // [graph|FTV] | ExtV
            m_splitter.TrackPane(1, showFileTypes,
                [this]() { m_splitter.SetSplitterPos(1.0); });
            m_subSplitter.TrackPane(0, showGraph,
                [this]() { m_subSplitter.SetSplitterPos(0.0); });
            break;
        case 2: // FTV | [ExtV|graph]
            m_subSplitter.TrackPane(0, showFileTypes,
                [this]() { m_subSplitter.SetSplitterPos(0.0); });
            m_subSplitter.TrackPane(1, showGraph,
                [this]() { m_subSplitter.SetSplitterPos(1.0); });
            break;
        case 3: // graph | [ExtV|FTV]
            m_splitter.TrackPane(0, showGraph,
                [this]() { m_splitter.SetSplitterPos(0.0); });
            m_subSplitter.TrackPane(0, showFileTypes,
                [this]() { m_subSplitter.SetSplitterPos(0.0); });
            break;
        }
        break;

    case LT_COLS_SUB_ROWS:
        m_splitter.TrackPane(1, showFileTypes,
            [this]() { m_splitter.SetSplitterPos(1.0); });
        m_subSplitter.TrackPane(perm == 0 ? 0 : 1, showGraph,
            [this, perm]() { m_subSplitter.SetSplitterPos(perm == 0 ? 0.0 : 1.0); });
        break;

    case LT_COLS_TM_FULL:
    {
        const int graphPane = (perm == 0 || perm == 1) ? 0 : 1;
        const int extPane = (perm == 0 || perm == 2) ? 0 : 1;
        m_splitter.TrackPane(graphPane, showGraph,
            [this, graphPane]() { m_splitter.SetSplitterPos(graphPane == 0 ? 0.0 : 1.0); });
        m_subSplitter.TrackPane(extPane, showFileTypes,
            [this, extPane]() { m_subSplitter.SetSplitterPos(extPane == 0 ? 0.0 : 1.0); });
        break;
    }
    }
}

void CMainFrame::BuildSplitterLayout(int topo, int perm, HWND hFTV, HWND hExtV, HWND hGraph)
{
    auto AttachView = [](CWdsSplitterWnd& splitter, int row, int col, HWND hView)
    {
        ::SetParent(hView, splitter.GetSafeHwnd());
        ::SetWindowLongPtr(hView, GWLP_ID, splitter.IdFromRowCol(row, col));
    };

    switch (topo)
    {
    case LT_ROWS_SUB_COLS:
        m_splitter.CreateStatic(this, 2, 1);
        if (perm == 0) // top: [FTV|ExtV], bottom: graph
        {
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(0, 0));
            AttachView(m_subSplitter, 0, 0, hFTV);
            AttachView(m_subSplitter, 0, 1, hExtV);
            AttachView(m_splitter, 1, 0, hGraph);
        }
        else // perm == 1: graph top, [FTV|ExtV] bottom
        {
            AttachView(m_splitter, 0, 0, hGraph);
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(1, 0));
            AttachView(m_subSplitter, 0, 0, hFTV);
            AttachView(m_subSplitter, 0, 1, hExtV);
        }
        break;

    case LT_COLS_THREE:
        m_splitter.CreateStatic(this, 1, 2);
        if (perm == 0) // [FTV|graph] in col 0, ExtV in col 1
        {
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(0, 0));
            AttachView(m_subSplitter, 0, 0, hFTV);
            AttachView(m_subSplitter, 0, 1, hGraph);
            AttachView(m_splitter, 0, 1, hExtV);
        }
        else if (perm == 1) // [graph|FTV] in col 0, ExtV in col 1
        {
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(0, 0));
            AttachView(m_subSplitter, 0, 0, hGraph);
            AttachView(m_subSplitter, 0, 1, hFTV);
            AttachView(m_splitter, 0, 1, hExtV);
        }
        else if (perm == 2) // FTV in col 0, [ExtV|graph] in col 1
        {
            AttachView(m_splitter, 0, 0, hFTV);
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(0, 1));
            AttachView(m_subSplitter, 0, 0, hExtV);
            AttachView(m_subSplitter, 0, 1, hGraph);
        }
        else // perm 3: graph in col 0, [ExtV|FTV] in col 1
        {
            AttachView(m_splitter, 0, 0, hGraph);
            m_subSplitter.CreateStatic(&m_splitter, 1, 2, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                       m_splitter.IdFromRowCol(0, 1));
            AttachView(m_subSplitter, 0, 0, hExtV);
            AttachView(m_subSplitter, 0, 1, hFTV);
        }
        break;

    case LT_COLS_SUB_ROWS:
        m_splitter.CreateStatic(this, 1, 2);
        m_subSplitter.CreateStatic(&m_splitter, 2, 1, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                   m_splitter.IdFromRowCol(0, 0));
        if (perm == 0) // left: graph/FTV; right: ExtV
        {
            AttachView(m_subSplitter, 0, 0, hGraph);
            AttachView(m_subSplitter, 1, 0, hFTV);
        }
        else // perm 1: left: FTV/graph; right: ExtV
        {
            AttachView(m_subSplitter, 0, 0, hFTV);
            AttachView(m_subSplitter, 1, 0, hGraph);
        }
        AttachView(m_splitter, 0, 1, hExtV);
        break;

    case LT_COLS_TM_FULL:
    {
        m_splitter.CreateStatic(this, 1, 2);
        const int graphCol = (perm == 0 || perm == 1) ? 0 : 1;
        const int extRow = (perm == 0 || perm == 2) ? 0 : 1; // ExtV on top (perm 0/2) or bottom (perm 1/3)
        AttachView(m_splitter, 0, graphCol, hGraph);
        m_subSplitter.CreateStatic(&m_splitter, 2, 1, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                   m_splitter.IdFromRowCol(0, 1 - graphCol));
        AttachView(m_subSplitter, extRow, 0, hExtV);
        AttachView(m_subSplitter, 1 - extRow, 0, hFTV);
        break;
    }
    }
}

void CMainFrame::RebuildLayout(bool resetPositions)
{
    int topo = COptions::LayoutTopology;
    int perm = COptions::LayoutPermutation;
    const bool isDefault = (topo == LT_ROWS_SUB_COLS && perm == 0);
    if (topo == 1 || (!isDefault && CLayoutPopup::LayoutIndex(topo, perm) == 0))
    {
        topo = LT_ROWS_SUB_COLS;
        perm = 0;
        COptions::LayoutTopology = topo;
        COptions::LayoutPermutation = perm;
    }

    // Capture view HWNDs; reparent to frame so DestroyWindow below doesn't kill them.
    const HWND hFTV   = GetFileTabbedView()->GetSafeHwnd();
    const HWND hExtV  = GetExtensionView()->GetSafeHwnd();
    const HWND hTMV   = GetTreeMapView()->GetSafeHwnd();
    const HWND hFGV   = GetFlameGraphView()->GetSafeHwnd();
    const HWND hFrame = GetSafeHwnd();
    const HWND hActiveGraph = COptions::UseFlameGraph ? hFGV : hTMV;
    const HWND hInactiveGraph = COptions::UseFlameGraph ? hTMV : hFGV;
    ::SetParent(hFTV,  hFrame);
    ::SetParent(hExtV, hFrame);
    ::SetParent(hTMV,  hFrame);
    ::SetParent(hFGV,  hFrame);
    ::SetWindowLongPtr(hInactiveGraph, GWLP_ID, ID_INACTIVE_GRAPH_PANE);

    if (m_splitter.GetSafeHwnd())
        m_splitter.DestroyWindow();

    if (resetPositions)
    {
        COptions::MainSplitterPos = -1.0;
        COptions::SubSplitterPos  = -1.0;
    }

    BuildSplitterLayout(topo, perm, hFTV, hExtV, hActiveGraph);
    ::ShowWindow(hActiveGraph, SW_SHOW);
    ::ShowWindow(hInactiveGraph, SW_HIDE);
    ::ShowWindow(hExtV, SW_SHOW);

    ConfigureSplitterCallbacks(topo, perm);
    m_splitter.SetStorage(COptions::MainSplitterPos.Ptr());
    m_subSplitter.SetStorage(COptions::SubSplitterPos.Ptr());
    RecalcLayout();

    switch (topo)
    {
    case LT_ROWS_SUB_COLS:
        m_splitter.RestoreSplitterPos(0.5);
        m_subSplitter.RestoreSplitterPos(0.75);
        break;
    case LT_COLS_THREE:
        if (perm == 0 || perm == 1)
        {
            m_splitter.RestoreSplitterPos(0.80);
            m_subSplitter.RestoreSplitterPos(0.50);
        }
        else // perm 2, 3: first col = 40%, sub in col 1 gets 60%
        {
            m_splitter.RestoreSplitterPos(0.40);
            m_subSplitter.RestoreSplitterPos(1.0 / 3.0);
        }
        break;
    case LT_COLS_SUB_ROWS:
        m_splitter.RestoreSplitterPos(0.75);
        m_subSplitter.RestoreSplitterPos(0.50);
        break;
    case LT_COLS_TM_FULL:
        m_splitter.RestoreSplitterPos(0.50);
        m_subSplitter.RestoreSplitterPos(0.50);
        break;
    }

    if (!GetExtensionView()->IsShowTypes())
        MinimizeExtensionView();
    if (!IsActiveGraphPaneShown())
        MinimizeGraphPane();

    DarkMode::AdjustControls(GetSafeHwnd());
    GetFileTabbedView()->RedrawWindow();
    GetActiveGraphPane()->RedrawWindow();
    GetExtensionView()->RedrawWindow();
}
