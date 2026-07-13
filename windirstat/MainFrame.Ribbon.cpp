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

//
// MainFrame.Ribbon.cpp
//
// Builds the optional Office-style tabbed ribbon (enabled via COptions::UseRibbon).
// The ribbon mirrors the classic menu: each top-level menu becomes a category (tab)
// and each separator-delimited section becomes a panel (group). Buttons reuse the
// existing command IDs, the shared Icons:: painters for icons, and the localization
// keys for labels, so command routing and ON_UPDATE_COMMAND_UI enable/check state all
// work exactly as they do for the menu and toolbar.
//

#include "pch.h"

#include "Controls/IconHandler.h"

#include <format>

namespace
{
    using Painter = std::function<void(Gdiplus::Graphics&)>;

    struct RibbonButton
    {
        UINT id = 0;
        std::wstring text;
        Painter painter; // optional; when set a 16px + 32px icon is generated
        bool large = false; // when true, rendered as a prominent 32px "hero" button
    };

    struct RibbonPanel
    {
        std::wstring name;
        std::vector<RibbonButton> buttons;
    };

    struct RibbonCategory
    {
        std::wstring name;
        std::vector<RibbonPanel> panels;
    };

    std::wstring L(const std::wstring_view key)
    {
        return Localization::Lookup(key);
    }
}

void CMainFrame::CreateRibbon()
{
    // Icon shorthands reused from the toolbar so the ribbon matches its look.
    const Painter refreshAll = Icons::Char(L'↻', RGB(0, 156, 221));
    const Painter search = Icons::Char(L'⌕', Icons::NeutralRef());
    const Painter zoomIn = [](Gdiplus::Graphics& g) { Icons::PaintMagnifier(g, true); };
    const Painter zoomOut = [](Gdiplus::Graphics& g) { Icons::PaintMagnifier(g, false); };

    std::vector<RibbonCategory> categories;

    // ---- File ----
    categories.push_back({ L(IDS_MENU_FILE),
    {
        { L(IDS_RIB_SCAN), {
            { ID_FILE_SELECT, L(IDS_MENU_SELECT), Icons::PaintFileSelect, true } } },
        { L(IDS_RIB_RESULTS), {
            { ID_LOAD_RESULTS, L(IDS_MENU_LOAD_RESULTS), {} },
            { ID_SAVE_RESULTS, L(IDS_MENU_SAVE_RESULTS), {} },
            { ID_SAVE_DUPLICATES, L(IDS_MENU_SAVE_DUPLICATES), {} },
            { ID_SAVE_PERMISSIONS, L(IDS_MENU_SAVE_PERMISSIONS), {} } } },
        { L(IDS_RIB_REFRESH), {
            { ID_REFRESH_ALL, L(IDS_MENU_REFRESH_ALL), refreshAll, true },
            { ID_REFRESH_SELECTED, L(IDS_MENU_REFRESH_SELECTED), Icons::PaintRefreshSelected } } },
        { L(IDS_RIB_APP), {
            { ID_RUN_ELEVATED, L(IDS_MENU_ELEVATED), {} },
            { ID_APP_EXIT, L(IDS_MENU_EXIT), {} } } },
    } });

    // ---- Edit ----
    categories.push_back({ L(IDS_MENU_EDIT),
    {
        { L(IDS_RIB_CLIPBOARD), {
            { ID_EDIT_COPY_CLIPBOARD, L(IDS_MENU_COPY_CLIPBOARD), Icons::PaintEditCopyClipboard, true } } },
        { L(IDS_RIB_FIND), {
            { ID_SEARCH, L(IDS_MENU_SEARCH), search, true },
            { ID_FILTER_EXCLUDE_ITEM, L(IDS_MENU_EXCLUDE_ITEM), [](Gdiplus::Graphics& g) { Icons::PaintFilter(g); } } } },
        { L(IDS_RIB_HASH), {
            { ID_COMPUTE_HASH, L(IDS_COMPUTE_HASH), {} } } },
    } });

    // ---- Clean Up ----
    RibbonCategory cleanup{ L(IDS_MENU_CLEANUP),
    {
        { L(IDS_RIB_OPEN), {
            { ID_CLEANUP_OPEN_SELECTED, L(IDS_MENU_OPEN), Icons::PaintOpenSelected, true },
            { ID_CLEANUP_EXPLORER_SELECT, L(IDS_MENU_EXPLORER_SELECT), Icons::PaintExplorerSelect },
            { ID_CLEANUP_OPEN_IN_CONSOLE, L(IDS_MENU_CONSOLE), Icons::PaintOpenInConsole },
            { ID_CLEANUP_OPEN_IN_PWSH, L(IDS_MENU_PWSH), {} },
            { ID_CLEANUP_PROPERTIES, L(IDS_MENU_PROPERTIES), Icons::PaintProperties } } },
        { L(IDS_RIB_DELETE), {
            { ID_CLEANUP_DELETE, L(IDS_MENU_DELETE), Icons::PaintDelete, true },
            { ID_CLEANUP_DELETE_BIN, L(IDS_MENU_DELETE_BIN), Icons::PaintDeleteBin },
            { ID_CLEANUP_EMPTY_BIN, L(IDS_MENU_EMPTY_BIN), {} },
            { ID_CLEANUP_EMPTY_FOLDER, L(IDS_MENU_EMPTY_FOLDER), {} },
            { ID_CLEANUP_MOVE_TO, L(IDS_MENU_MOVE_TO), {} } } },
        { L(IDS_RIB_COMPRESS), {
            { ID_COMPRESS_NONE, L(IDS_MENU_COMPRESS_NONE), {} },
            { ID_COMPRESS_LZNT1, L(IDS_MENU_COMPRESS_LZNT1), {} },
            { ID_COMPRESS_XPRESS4K, L(IDS_MENU_COMPRESS_XPRESS4K), {} },
            { ID_COMPRESS_XPRESS8K, L(IDS_MENU_COMPRESS_XPRESS8K), {} },
            { ID_COMPRESS_XPRESS16K, L(IDS_MENU_COMPRESS_XPRESS16K), {} },
            { ID_COMPRESS_LZX, L(IDS_MENU_COMPRESS_LZX), {} } } },
        { L(IDS_RIB_ADVANCED), {
            { ID_CLEANUP_SPARSIFY_FILE, L(IDS_MENU_SPARSIFY_FILE), {} },
            { ID_CLEANUP_OPTIMIZE_VHD, L(IDS_MENU_OPTIMIZE_VHD), {} },
            { ID_CLEANUP_CREATE_HARDLINK, L(IDS_MENU_CREATE_HARDLINK), {} },
            { ID_CLEANUP_REMOVE_MOTW, L(IDS_MENU_REMOVE_MOTW), {} } } },
        { L(IDS_RIB_SYSTEM), {
            { ID_CLEANUP_HIBERNATE, L(IDS_MENU_DISABLE_HIBERNATE), {} },
            { ID_CLEANUP_DISK_CLEANUP, L(IDS_MENU_DISK_CLEANUP), {} },
            { ID_CLEANUP_STORAGE_SENSE, L(IDS_MENU_STORAGE_SENSE), {} },
            { ID_CLEANUP_REMOVE_PROGRAMS, L(IDS_MENU_REMOVE_PROGRAMS), {} },
            { ID_CLEANUP_REMOVE_ROAMING, L(IDS_MENU_REMOVE_ROAMING), {} },
            { ID_CLEANUP_REMOVE_LOCAL, L(IDS_MENU_REMOVE_LOCAL), {} },
            { ID_CLEANUP_REMOVE_SHADOW, L(IDS_MENU_REMOVE_SHADOW), {} },
            { ID_CLEANUP_DISM_ANALYZE, L"/AnalyzeComponentStore", {} },
            { ID_CLEANUP_DISM_NORMAL, L"/StartComponentCleanup", {} },
            { ID_CLEANUP_DISM_RESET, L"/StartComponentCleanup /ResetBase", {} } } },
    } };

    // User-defined cleanups (dynamic, only the enabled ones)
    RibbonPanel customCleanup{ L(IDS_RIB_CUSTOM) };
    for (UINT i = 0; i < COptions::UserDefinedCleanups.size(); i++)
    {
        auto& udc = COptions::UserDefinedCleanups[i];
        if (!udc.Enabled) continue;
        customCleanup.buttons.push_back({ ID_USERDEFINEDCLEANUP0 + i, udc.Title.Obj(), {} });
    }
    if (!customCleanup.buttons.empty()) cleanup.panels.push_back(std::move(customCleanup));
    categories.push_back(std::move(cleanup));

    // ---- View ----
    categories.push_back({ L(IDS_MENU_VIEW),
    {
        { L(IDS_RIB_SHOW), {
            { ID_VIEW_SHOWTREEMAP, L(IDS_MENU_SHOW), {} },
            { ID_VIEW_FLAMEGRAPH, L(IDS_MENU_FLAMEGRAPH), {} } } },
        { L(IDS_RIB_SIZE), {
            { ID_TREEMAP_LOGICAL_SIZE, L(IDS_MENU_LOGICAL_SIZE), {} },
            { ID_TREEMAP_PHYSICAL_SIZE, L(IDS_MENU_PHYSICAL_SIZE), {} } } },
        { L(IDS_RIB_TREEMAP), {
            { ID_TREEMAP_SHOW_EXTENSIONS, L(IDS_MENU_TREEMAP_SHOW_EXTENSIONS), {} },
            { ID_TREEMAP_SHOW_FOLDER_FRAMES, L(IDS_MENU_TREEMAP_SHOW_FOLDER_FRAMES), {} } } },
        { L(IDS_RIB_ZOOM), {
            { ID_TREEMAP_ZOOMIN, L(IDS_MENU_ZOOMIN), zoomIn, true },
            { ID_TREEMAP_ZOOMOUT, L(IDS_MENU_ZOOMOUT), zoomOut },
            { ID_TREEMAP_ZOOMRESET, L(IDS_MENU_ZOOMRESET), {} },
            { ID_TREEMAP_RESELECT_CHILD, L(IDS_MENU_RESELECT_CHILD), {} },
            { ID_TREEMAP_SELECT_PARENT, L(IDS_MENU_SELECT_PARENT), {} } } },
    } });

    // ---- Tools ----
    RibbonCategory tools{ L(IDS_MENU_TOOLS),
    {
        { L(IDS_RIB_ANALYZE), {
            { ID_TOOLS_WATCHER, L(IDS_MENU_WATCHER), {} },
            { ID_TOOLS_PERMISSIONS, L(IDS_MENU_PERMISSIONS), {} },
            { ID_TOOLS_STORAGE_ANALYTICS, L(IDS_MENU_STORAGE_ANALYTICS), {} } } },
        { L(IDS_RIB_MODIFY), {
            { ID_TOOLS_SET_DATES, L(IDS_MENU_SET_DATES), {} },
            { ID_TOOLS_REMOVE_EMPTY, L(IDS_MENU_REMOVE_EMPTY), {} } } },
    } };

    // Per-drive disk-maintenance panels (built once at ribbon creation)
    RibbonPanel shadow{ L(IDS_RIB_SHADOW) };
    RibbonPanel defrag{ L(IDS_RIB_DEFRAG) };
    RibbonPanel chkdsk{ L(IDS_RIB_CHKDSK) };
    for (const auto& drive : GetDriveList({ DRIVE_FIXED, DRIVE_REMOVABLE, DRIVE_RAMDISK }))
    {
        const std::wstring volumeName = GetVolumeName(drive);
        const std::wstring displayName = volumeName.empty()
            ? GetDrive(drive) : std::format(L"{:.2} ({})", drive, volumeName);
        const int driveIndex = std::toupper(drive[0]) - L'A';
        shadow.buttons.push_back({ static_cast<UINT>(ID_TOOLS_SHADOW_COPY_BASE + driveIndex), displayName, {} });
        defrag.buttons.push_back({ static_cast<UINT>(ID_TOOLS_DEFRAG_BASE + driveIndex), displayName, {} });
        chkdsk.buttons.push_back({ static_cast<UINT>(ID_TOOLS_CHKDSK_BASE + driveIndex), displayName, {} });
    }
    if (!shadow.buttons.empty()) tools.panels.push_back(std::move(shadow));
    if (!defrag.buttons.empty()) tools.panels.push_back(std::move(defrag));
    if (!chkdsk.buttons.empty()) tools.panels.push_back(std::move(chkdsk));
    categories.push_back(std::move(tools));

    // ---- Options ----
    categories.push_back({ L(IDS_MENU_OPTIONS),
    {
        { L(IDS_RIB_DISPLAY), {
            { ID_VIEW_SHOWFREESPACE, L(IDS_MENU_FREE_SPACE), {} },
            { ID_VIEW_SHOWUNKNOWN, L(IDS_MENU_SHOW_UNKNOWN), {} },
            { ID_VIEW_SHOWFILETYPES, L(IDS_MENU_FILE_TYPES), {} },
            { ID_VIEW_GROUP_TYPES, L(IDS_MENU_GROUP_TYPES), {} } } },
        { L(IDS_RIB_INTERFACE), {
            { ID_VIEW_TOOLBAR, L(IDS_MENU_TOOL_BAR), {} },
            { ID_VIEW_LARGE_TOOLBAR, L(IDS_MENU_LARGE_TOOLBAR), {} },
            { ID_VIEW_STATUS_BAR, L(IDS_MENU_STATUS_BAR), {} },
            { ID_VIEW_RIBBON, L(IDS_MENU_RIBBON), {} } } },
        { L(IDS_RIB_SETTINGS), {
            { ID_CONFIGURE, L(IDS_MENU_SETTINGS), Icons::PaintGear, true },
            { ID_VIEW_WINDOW_LAYOUT, L(IDS_WINDOW_LAYOUT), Icons::PaintWindowLayout, true } } },
    } });

    // ---- Help ----
    categories.push_back({ L(IDS_MENU_HELP),
    {
        { L(IDS_RIB_HELP), {
            { ID_HELP_MANUAL, L(IDS_MENU_HELP), Icons::PaintHelp, true },
            { ID_HELP_REPORTBUG, L(IDS_MENU_HELP_REPORT), {} },
            { ID_APP_ABOUT, L(IDS_MENU_HELP_ABOUT), {} } } },
    } });

    // Materialize the ribbon from the description above.
    m_wndRibbonBar.Create(this);
    m_wndRibbonBar.EnableToolTips();

    for (auto& category : categories)
    {
        if (category.panels.empty()) continue;

        CMFCRibbonCategory* cat = m_wndRibbonBar.AddCategory(category.name.c_str(),
            0, 0, CSize(16, 16), CSize(32, 32));

        CMFCToolBarImages& smallImages = cat->GetSmallImages();
        CMFCToolBarImages& largeImages = cat->GetLargeImages();
        smallImages.SetImageSize(CSize(16, 16));
        largeImages.SetImageSize(CSize(32, 32));

        for (auto& panelDef : category.panels)
        {
            if (panelDef.buttons.empty()) continue;
            CMFCRibbonPanel* panel = cat->AddPanel(panelDef.name.c_str());

            for (auto& button : panelDef.buttons)
            {
                int smallIndex = -1;
                int largeIndex = -1;
                if (button.painter)
                {
                    CBitmap bmpSmall;
                    bmpSmall.Attach(Icons::MakeBitmap(16, button.painter));
                    smallIndex = smallImages.AddImage(bmpSmall, TRUE);

                    CBitmap bmpLarge;
                    bmpLarge.Attach(Icons::MakeBitmap(32, button.painter));
                    largeIndex = largeImages.AddImage(bmpLarge, TRUE);
                }

                auto* ribbonButton = new CMFCRibbonButton(button.id, button.text.c_str(),
                    smallIndex, largeIndex);

                // Promote flagged commands to prominent 32px "hero" buttons. This is
                // what gives the ribbon its Office-style visual hierarchy (one or two
                // large buttons per group with the remaining commands stacked small)
                // instead of a flat, uniform row of identical buttons.
                if (button.large && largeIndex != -1)
                {
                    ribbonButton->SetAlwaysLargeImage();
                }

                panel->Add(ribbonButton);
            }
        }
    }
}
