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
        bool checkbox = false; // when true, rendered as a ribbon check box that reflects on/off state
        std::vector<RibbonButton> subItems; // when non-empty, rendered as a drop-down menu button
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

    // Additional glyph icons for commands that previously had none. All glyphs are
    // drawn from Segoe UI Symbol (the font used for every other generated icon) so
    // they render consistently in both light and dark mode.
    //
    // Save/Load use a hand-drawn "block arrow" instead of a font glyph so the arrow
    // is short, fat, and shapely: a wide chevron head over a stubby shaft, with a
    // round-join outline that softens the corners. Drawn in the shared 64x64 icon
    // space and flipped vertically for the download (load) variant.
    const auto blockArrow = [](Gdiplus::Graphics& g, const bool up)
    {
        using namespace Gdiplus;
        const Color c = Icons::Neutral();
        SolidBrush brush(c);
        Pen pen(c, 7.0f);
        pen.SetLineJoin(LineJoinRound);
        PointF pts[] = {
            { 32.0f, 14.0f }, // head apex
            { 53.0f, 34.0f }, // head right corner
            { 40.0f, 34.0f }, // right shoulder
            { 40.0f, 50.0f }, // shaft bottom-right
            { 24.0f, 50.0f }, // shaft bottom-left
            { 24.0f, 34.0f }, // left shoulder
            { 11.0f, 34.0f }  // head left corner
        };
        if (!up) for (auto& p : pts) p.Y = 64.0f - p.Y;
        g.FillPolygon(&brush, pts, static_cast<INT>(std::size(pts)));
        g.DrawPolygon(&pen, pts, static_cast<INT>(std::size(pts)));
    };
    const Painter loadIcon    = [blockArrow](Gdiplus::Graphics& g) { blockArrow(g, false); };
    const Painter saveIcon    = [blockArrow](Gdiplus::Graphics& g) { blockArrow(g, true); };
    const Painter hashIcon    = Icons::Char(L'#', Icons::NeutralRef());
    const Painter treemapIcon = Icons::Char(L'▦', Icons::NeutralRef());
    const Painter aboutIcon   = Icons::Char(L'ℹ', Icons::NeutralRef());
    const Painter exitIcon    = Icons::Char(L'✕', Icons::NeutralRef());
    const Painter moveIcon    = Icons::Char(L'→', Icons::NeutralRef());
    const Painter consoleIcon = [](Gdiplus::Graphics& g) { Icons::PaintOpenInConsole(g); };

    // Neutral single-glyph icon helper for the many commands that otherwise ship no
    // artwork. Every glyph used below was verified to exist in Segoe UI Symbol (the
    // font all generated icons use), so none render as a "missing glyph" box.
    const auto ic = [](const WCHAR ch) { return Icons::Char(ch, Icons::NeutralRef()); };

    // Shared icons for the dynamically-built per-drive maintenance panels.
    const Painter shadowIcon = ic(L'◷'); // snapshot / point-in-time copy
    const Painter defragIcon = ic(L'◫'); // reorganized disk blocks
    const Painter chkdskIcon = ic(L'☑'); // verified / checked volume
    const Painter customIcon = ic(L'★'); // user-defined cleanup

    std::vector<RibbonCategory> categories;

    // ---- File ----
    categories.push_back({ L(IDS_MENU_FILE),
    {
        { L(IDS_RIB_SCAN), {
            { ID_FILE_SELECT, L(IDS_MENU_SELECT), Icons::PaintFileSelect, true } } },
        { L(IDS_RIB_RESULTS), {
            { ID_LOAD_RESULTS, L(IDS_MENU_LOAD_RESULTS), loadIcon },
            { ID_SAVE_RESULTS, L(IDS_MENU_SAVE_RESULTS), saveIcon },
            { ID_SAVE_DUPLICATES, L(IDS_MENU_SAVE_DUPLICATES), saveIcon },
            { ID_SAVE_PERMISSIONS, L(IDS_MENU_SAVE_PERMISSIONS), saveIcon } } },
        { L(IDS_RIB_REFRESH), {
            { ID_REFRESH_ALL, L(IDS_MENU_REFRESH_ALL), refreshAll, true },
            { ID_REFRESH_SELECTED, L(IDS_MENU_REFRESH_SELECTED), Icons::PaintRefreshSelected } } },
        { L(IDS_RIB_APP), {
            { ID_RUN_ELEVATED, L(IDS_MENU_ELEVATED), ic(L'⇧') },
            { ID_APP_EXIT, L(IDS_MENU_EXIT), exitIcon } } },
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
            { ID_COMPUTE_HASH, L(IDS_COMPUTE_HASH), hashIcon } } },
    } });

    // ---- Clean Up ----
    RibbonCategory cleanup{ L(IDS_MENU_CLEANUP),
    {
        { L(IDS_RIB_OPEN), {
            { ID_CLEANUP_OPEN_SELECTED, L(IDS_MENU_OPEN), Icons::PaintOpenSelected, true },
            { ID_CLEANUP_EXPLORER_SELECT, L(IDS_MENU_EXPLORER_SELECT), Icons::PaintExplorerSelect },
            { ID_CLEANUP_OPEN_IN_CONSOLE, L(IDS_MENU_CONSOLE), Icons::PaintOpenInConsole },
            { ID_CLEANUP_OPEN_IN_PWSH, L(IDS_MENU_PWSH), consoleIcon },
            { ID_CLEANUP_PROPERTIES, L(IDS_MENU_PROPERTIES), Icons::PaintProperties } } },
        { L(IDS_RIB_DELETE), {
            { ID_CLEANUP_DELETE, L(IDS_MENU_DELETE), Icons::PaintDelete, true },
            { ID_CLEANUP_DELETE_BIN, L(IDS_MENU_DELETE_BIN), Icons::PaintDeleteBin },
            { ID_CLEANUP_EMPTY_BIN, L(IDS_MENU_EMPTY_BIN), ic(L'♻') },
            { ID_CLEANUP_EMPTY_FOLDER, L(IDS_MENU_EMPTY_FOLDER), ic(L'∅') },
            { ID_CLEANUP_MOVE_TO, L(IDS_MENU_MOVE_TO), moveIcon } } },
        { L(IDS_RIB_COMPRESS), {
            // The six compression levels collapse into a single "Compress" drop-down
            // (a prominent hero button) so the panel needs one column instead of two.
            { 0, L(IDS_RIB_COMPRESS), ic(L'⇲'), true, false, {
                { ID_COMPRESS_NONE, L(IDS_MENU_COMPRESS_NONE), ic(L'⊖') },
                { ID_COMPRESS_LZNT1, L(IDS_MENU_COMPRESS_LZNT1), ic(L'⇲') },
                { ID_COMPRESS_XPRESS4K, L(IDS_MENU_COMPRESS_XPRESS4K), ic(L'⇲') },
                { ID_COMPRESS_XPRESS8K, L(IDS_MENU_COMPRESS_XPRESS8K), ic(L'⇲') },
                { ID_COMPRESS_XPRESS16K, L(IDS_MENU_COMPRESS_XPRESS16K), ic(L'⇲') },
                { ID_COMPRESS_LZX, L(IDS_MENU_COMPRESS_LZX), ic(L'⇲') } } } } },
        { L(IDS_RIB_ADVANCED), {
            { ID_CLEANUP_SPARSIFY_FILE, L(IDS_MENU_SPARSIFY_FILE), ic(L'✂') },
            { ID_CLEANUP_OPTIMIZE_VHD, L(IDS_MENU_OPTIMIZE_VHD), ic(L'⟳') },
            { ID_CLEANUP_CREATE_HARDLINK, L(IDS_MENU_CREATE_HARDLINK), ic(L'⇄') },
            { ID_CLEANUP_REMOVE_MOTW, L(IDS_MENU_REMOVE_MOTW), ic(L'⚐') } } },
        { L(IDS_RIB_SYSTEM), {
            { ID_CLEANUP_HIBERNATE, L(IDS_MENU_DISABLE_HIBERNATE), ic(L'☾') },
            { ID_CLEANUP_DISK_CLEANUP, L(IDS_MENU_DISK_CLEANUP), ic(L'⊛') },
            { ID_CLEANUP_STORAGE_SENSE, L(IDS_MENU_STORAGE_SENSE), ic(L'◷') },
            { ID_CLEANUP_REMOVE_PROGRAMS, L(IDS_MENU_REMOVE_PROGRAMS), ic(L'⊞') },
            // Profile / shadow-copy removals collapse into a single "Remove" drop-down.
            { 0, L(IDS_RIB_REMOVE), ic(L'⊖'), false, false, {
                { ID_CLEANUP_REMOVE_ROAMING, L(IDS_MENU_REMOVE_ROAMING), ic(L'⊖') },
                { ID_CLEANUP_REMOVE_LOCAL, L(IDS_MENU_REMOVE_LOCAL), ic(L'⊖') },
                { ID_CLEANUP_REMOVE_SHADOW, L(IDS_MENU_REMOVE_SHADOW), ic(L'⊖') } } },
            // The three DISM component-store actions collapse into one drop-down.
            { 0, L(IDS_MENU_DISM), ic(L'⚙'), false, false, {
                { ID_CLEANUP_DISM_ANALYZE, L"/AnalyzeComponentStore", ic(L'⚙') },
                { ID_CLEANUP_DISM_NORMAL, L"/StartComponentCleanup", ic(L'⚙') },
                { ID_CLEANUP_DISM_RESET, L"/StartComponentCleanup /ResetBase", ic(L'⚙') } } } } },
    } };

    // User-defined cleanups (dynamic, only the enabled ones)
    RibbonPanel customCleanup{ L(IDS_RIB_CUSTOM) };
    for (UINT i = 0; i < COptions::UserDefinedCleanups.size(); i++)
    {
        auto& udc = COptions::UserDefinedCleanups[i];
        if (!udc.Enabled) continue;
        customCleanup.buttons.push_back({ ID_USERDEFINEDCLEANUP0 + i, udc.Title.Obj(), customIcon });
    }
    if (!customCleanup.buttons.empty()) cleanup.panels.push_back(std::move(customCleanup));
    categories.push_back(std::move(cleanup));

    // ---- View ----
    categories.push_back({ L(IDS_MENU_VIEW),
    {
        { L(IDS_RIB_SHOW), {
            { ID_VIEW_SHOWTREEMAP, L(IDS_MENU_SHOW), treemapIcon },
            { ID_VIEW_FLAMEGRAPH, L(IDS_MENU_FLAMEGRAPH), ic(L'☰') } } },
        { L(IDS_RIB_SIZE), {
            { ID_TREEMAP_LOGICAL_SIZE, L(IDS_MENU_LOGICAL_SIZE), {} },
            { ID_TREEMAP_PHYSICAL_SIZE, L(IDS_MENU_PHYSICAL_SIZE), {} } } },
        { L(IDS_RIB_TREEMAP), {
            { ID_TREEMAP_SHOW_EXTENSIONS, L(IDS_MENU_TREEMAP_SHOW_EXTENSIONS), {}, false, true },
            { ID_TREEMAP_SHOW_FOLDER_FRAMES, L(IDS_MENU_TREEMAP_SHOW_FOLDER_FRAMES), {}, false, true } } },
        { L(IDS_RIB_ZOOM), {
            { ID_TREEMAP_ZOOMIN, L(IDS_MENU_ZOOMIN), zoomIn, true },
            { ID_TREEMAP_ZOOMOUT, L(IDS_MENU_ZOOMOUT), zoomOut },
            { ID_TREEMAP_ZOOMRESET, L(IDS_MENU_ZOOMRESET), ic(L'⟲') },
            { ID_TREEMAP_RESELECT_CHILD, L(IDS_MENU_RESELECT_CHILD), ic(L'↧') },
            { ID_TREEMAP_SELECT_PARENT, L(IDS_MENU_SELECT_PARENT), ic(L'↥') } } },
    } });

    // ---- Tools ----
    RibbonCategory tools{ L(IDS_MENU_TOOLS),
    {
        { L(IDS_RIB_ANALYZE), {
            { ID_TOOLS_WATCHER, L(IDS_MENU_WATCHER), ic(L'◉') },
            { ID_TOOLS_PERMISSIONS, L(IDS_MENU_PERMISSIONS), ic(L'⛨') },
            { ID_TOOLS_STORAGE_ANALYTICS, L(IDS_MENU_STORAGE_ANALYTICS), ic(L'▤') } } },
        { L(IDS_RIB_MODIFY), {
            { ID_TOOLS_SET_DATES, L(IDS_MENU_SET_DATES), ic(L'⏱') },
            { ID_TOOLS_REMOVE_EMPTY, L(IDS_MENU_REMOVE_EMPTY), ic(L'⊖') } } },
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
        shadow.buttons.push_back({ static_cast<UINT>(ID_TOOLS_SHADOW_COPY_BASE + driveIndex), displayName, shadowIcon });
        defrag.buttons.push_back({ static_cast<UINT>(ID_TOOLS_DEFRAG_BASE + driveIndex), displayName, defragIcon });
        chkdsk.buttons.push_back({ static_cast<UINT>(ID_TOOLS_CHKDSK_BASE + driveIndex), displayName, chkdskIcon });
    }
    if (!shadow.buttons.empty()) tools.panels.push_back(std::move(shadow));
    if (!defrag.buttons.empty()) tools.panels.push_back(std::move(defrag));
    if (!chkdsk.buttons.empty()) tools.panels.push_back(std::move(chkdsk));
    categories.push_back(std::move(tools));

    // ---- Options ----
    categories.push_back({ L(IDS_MENU_OPTIONS),
    {
        { L(IDS_RIB_DISPLAY), {
            { ID_VIEW_SHOWFREESPACE, L(IDS_MENU_FREE_SPACE), {}, false, true },
            { ID_VIEW_SHOWUNKNOWN, L(IDS_MENU_SHOW_UNKNOWN), {}, false, true },
            { ID_VIEW_SHOWFILETYPES, L(IDS_MENU_FILE_TYPES), {}, false, true },
            { ID_VIEW_GROUP_TYPES, L(IDS_MENU_GROUP_TYPES), {}, false, true } } },
        { L(IDS_RIB_INTERFACE), {
            { ID_VIEW_TOOLBAR, L(IDS_MENU_TOOL_BAR), {}, false, true },
            { ID_VIEW_LARGE_TOOLBAR, L(IDS_MENU_LARGE_TOOLBAR), {}, false, true },
            { ID_VIEW_STATUS_BAR, L(IDS_MENU_STATUS_BAR), {}, false, true },
            { ID_VIEW_RIBBON, L(IDS_MENU_RIBBON), {}, false, true } } },
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
            { ID_APP_ABOUT, L(IDS_MENU_HELP_ABOUT), aboutIcon } } },
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

        // Builds a CMFCRibbonButton, registering its 16px/32px icons in the category
        // image lists. Shared by top-level buttons and drop-down sub-items so both get
        // artwork the same way.
        auto buildButton = [&](const RibbonButton& button)
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
            return new CMFCRibbonButton(button.id, button.text.c_str(), smallIndex, largeIndex);
        };

        for (auto& panelDef : category.panels)
        {
            if (panelDef.buttons.empty()) continue;
            CMFCRibbonPanel* panel = cat->AddPanel(panelDef.name.c_str());

            for (auto& button : panelDef.buttons)
            {
                // Boolean options render as ribbon check boxes so their on/off state
                // is visible directly in the ribbon (driven by the existing
                // ON_UPDATE_COMMAND_UI SetCheck handlers), instead of as plain buttons
                // that give no indication of whether the option is currently enabled.
                if (button.checkbox)
                {
                    panel->Add(new CMFCRibbonCheckBox(button.id, button.text.c_str()));
                    continue;
                }

                CMFCRibbonButton* ribbonButton = buildButton(button);

                // A button with sub-items becomes a drop-down (menu) button: several
                // related commands collapse behind one button whose popup lists them,
                // using vertical space to keep the ribbon narrow. The parent carries
                // id 0, so MFC keeps it enabled purely to open its menu.
                if (!button.subItems.empty())
                {
                    for (const auto& sub : button.subItems)
                    {
                        ribbonButton->AddSubItem(buildButton(sub));
                    }
                }

                // Promote flagged commands to prominent 32px "hero" buttons. This is
                // what gives the ribbon its Office-style visual hierarchy (one or two
                // large buttons per group with the remaining commands stacked small)
                // instead of a flat, uniform row of identical buttons.
                if (button.large && button.painter)
                {
                    ribbonButton->SetAlwaysLargeImage();
                }

                panel->Add(ribbonButton);
            }
        }
    }
}
