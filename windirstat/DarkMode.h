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

class DarkMode final
{
public:

    // window messages related to menu bar drawing
    static constexpr auto WM_UAHDRAWMENU = 0x0091;
    static constexpr auto WM_UAHDRAWMENUITEM = 0x0092;

    // Check if dark mode is supported on this system
    static bool IsDarkModeActive() noexcept { return s_darkModeEnabled; }
    static bool EnhancedDarkModeSupport();
    static COLORREF WdsSysColor(DWORD index);

    // Menu rendering functions
    static void DrawMenuClientArea(CWnd& wnd);

    // Unified message handler for menu drawing messages
    static LRESULT HandleMenuMessage(UINT message, WPARAM wParam, LPARAM lParam, HWND hWnd);

    // Enhanced dialog support functions
    static HBRUSH GetDialogBackgroundBrush();
    static void AdjustControls(HWND hWnd);
    static HBRUSH OnCtlColor(CDC* pDC, UINT nCtlColor);
    static void SetAppDarkMode() noexcept;
    static void SetupGlobalColors() noexcept;
    static void LightenBitmap(CBitmap* pBitmap, bool invert = false);
    static void DrawFocusRect(CDC* pdc, const CRect& rc);

private:
    static bool s_darkModeEnabled;
};

//
// CTabCtrlHelper. Used to set up tab control properties.
//
class CTabCtrlHelper final : public CMFCTabCtrl
{
public:
    static void SetupTabControl(CMFCTabCtrl& tab, Style lightStyle = STYLE_3D_VS2005)
    {
        auto& helper = reinterpret_cast<CTabCtrlHelper&>(tab);

        helper.ModifyTabStyle(DarkMode::IsDarkModeActive() ? STYLE_FLAT : lightStyle);
        helper.EnableTabSwap(FALSE);
        helper.SetDrawFrame(TRUE);
        helper.SetScrollButtons();
        helper.SetActiveTabBoldFont();

        // Forcibly hide scroll controls
        for (auto* const btn : { &helper.m_btnScrollFirst, &helper.m_btnScrollLast, &helper.m_btnScrollLeft, &helper.m_btnScrollRight })
        {
            if (IsWindow(*btn)) btn->ShowWindow(SW_HIDE);
        }
        helper.m_bScroll = FALSE;

        // Dark mode: give the active tab an accent background with contrasting
        // text. Without an explicit text color the active tab text defaults to
        // the (light) window-text color, which is unreadable on a light active
        // tab background - so set both the background and the foreground here.
        if (DarkMode::IsDarkModeActive())
        {
            helper.SetActiveTabColor(DarkMode::WdsSysColor(COLOR_HIGHLIGHT));
            helper.SetActiveTabTextColor(DarkMode::WdsSysColor(COLOR_HIGHLIGHTTEXT));
            helper.SetTabBorderSize(1);
        }
    }
};

//
// CDarkModeVisualManager. A visual manager tweak for dark mode support
//
class CDarkModeVisualManager final : public CMFCVisualManagerWindows
{
    DECLARE_DYNCREATE(CDarkModeVisualManager)

protected:

    void GetTabFrameColors(const CMFCBaseTabCtrl* pTabWnd,
        COLORREF& clrDark, COLORREF& clrBlack, COLORREF& clrHighlight, COLORREF& clrFace,
        COLORREF& clrDarkShadow, COLORREF& clrLight, CBrush*& pbrFace, CBrush*& pbrBlack) override;
    void OnFillBarBackground(CDC* pDC, CBasePane* pBar, CRect rectClient, CRect rectClip, BOOL bNCArea) override;
    void OnDrawSeparator(CDC* pDC, CBasePane* pBar, CRect rect, BOOL bIsHoriz) override;
    void OnDrawStatusBarPaneBorder(CDC* pDC, CMFCStatusBar* pBar, CRect rectPane, UINT uiID, UINT nStyle) override;
    void OnFillSplitterBackground(CDC* pDC, CSplitterWndEx* pSplitterWnd, CRect rect) override;
    void OnUpdateSystemColors() override;

    // Flat, modern ribbon rendering. The stock CMFCVisualManagerWindows draws the
    // ribbon with raised 3D borders, polygon "raised" tabs, dithered checked fills
    // and light-mode caption colors - a dated, high-relief look. These overrides
    // replace all of that with flat fills, an accent underline on the active tab,
    // thin separators and dark panel captions.
    COLORREF OnDrawRibbonTabsFrame(CDC* pDC, CMFCRibbonBar* pWndRibbonBar, CRect rectTab) override;
    void OnDrawRibbonCategory(CDC* pDC, CMFCRibbonCategory* pCategory, CRect rectCategory) override;
    COLORREF OnDrawRibbonCategoryTab(CDC* pDC, CMFCRibbonTab* pTab, BOOL bIsActive) override;
    COLORREF OnDrawRibbonPanel(CDC* pDC, CMFCRibbonPanel* pPanel, CRect rectPanel, CRect rectCaption) override;
    void OnDrawRibbonPanelCaption(CDC* pDC, CMFCRibbonPanel* pPanel, CRect rectCaption) override;
    COLORREF OnFillRibbonButton(CDC* pDC, CMFCRibbonButton* pButton) override;
    void OnDrawRibbonButtonBorder(CDC* pDC, CMFCRibbonButton* pButton) override;
};
