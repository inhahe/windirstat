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
#include "HangDump.h"

#include <dbghelp.h>

// Link the debug-help library that provides MiniDumpWriteDump.
#pragma comment(lib, "dbghelp.lib")

namespace
{
    // ---- installed state -------------------------------------------------

    std::atomic<HWND> g_hwndToMonitor{ nullptr };
    HANDLE g_watchdogThread = nullptr;
    HANDLE g_stopEvent = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
    std::mutex g_dumpMutex;                 // serialize concurrent dump requests
    std::atomic<bool> g_dumpInProgress{ false };

    // ---- environment helpers ---------------------------------------------

    std::wstring GetEnv(const wchar_t* name)
    {
        wchar_t buffer[MAX_PATH];
        const DWORD len = ::GetEnvironmentVariable(name, buffer, static_cast<DWORD>(std::size(buffer)));
        if (len == 0 || len >= std::size(buffer)) return {};
        return std::wstring(buffer, len);
    }

    bool EnvDisabled(const wchar_t* name)
    {
        const std::wstring v = GetEnv(name);
        return v == L"0" || v == L"false" || v == L"FALSE";
    }

    bool EnvEnabled(const wchar_t* name)
    {
        const std::wstring v = GetEnv(name);
        return v == L"1" || v == L"true" || v == L"TRUE";
    }

    unsigned GetThresholdSeconds()
    {
        const std::wstring v = GetEnv(L"WDS_HANGDUMP_SECONDS");
        if (v.empty()) return 15;
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(v.c_str(), &end, 10);
        if (end == v.c_str() || parsed == 0) return 15;
        return parsed < 3 ? 3u : static_cast<unsigned>(parsed);
    }

    std::wstring GetDumpDirectory()
    {
        // Explicit override wins.
        if (std::wstring dir = GetEnv(L"WDS_HANGDUMP_DIR"); !dir.empty())
        {
            return dir;
        }

        // Otherwise %TEMP%\WinDirStat, which is writable even when the exe lives
        // in a read-only location such as Program Files.
        wchar_t temp[MAX_PATH];
        const DWORD len = ::GetTempPath(static_cast<DWORD>(std::size(temp)), temp);
        std::wstring base = (len > 0 && len < std::size(temp)) ? std::wstring(temp, len) : std::wstring(L".\\");
        if (!base.empty() && base.back() != L'\\') base.push_back(L'\\');
        return base + L"WinDirStat";
    }

    void CopyTextToClipboard(const std::wstring& text)
    {
        if (!::OpenClipboard(nullptr)) return;
        ::EmptyClipboard();

        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (const HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* ptr = ::GlobalLock(hMem))
            {
                memcpy(ptr, text.c_str(), bytes);
                ::GlobalUnlock(hMem);
                ::SetClipboardData(CF_UNICODETEXT, hMem);
            }
            else
            {
                ::GlobalFree(hMem);
            }
        }
        ::CloseClipboard();
    }

    // Asks the monitored UI thread to acknowledge a no-op message. WM_NULL is
    // handled instantly by a thread that is pumping, so a timeout (or
    // SMTO_ABORTIFHUNG tripping the system's "not responding" flag) means it is
    // not pumping. Returns true when there is nothing to judge, so callers treat
    // "no window" as "not hung".
    bool IsUiResponsive(const DWORD timeoutMs)
    {
        const HWND hwnd = g_hwndToMonitor.load();
        if (hwnd == nullptr || !::IsWindow(hwnd)) return true;

        DWORD_PTR result = 0;
        return ::SendMessageTimeout(hwnd, WM_NULL, 0, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result) != 0;
    }

    void NotifyDumpWritten(const std::wstring& path, const ULONGLONG stalledMs)
    {
        if (EnvEnabled(L"WDS_HANGDUMP_SILENT")) return;

        // Runs on the watchdog thread, so a modal message box here does not
        // depend on the (possibly wedged) UI thread. Keep the owner null so it
        // is independent of any hung window.

        // Use TaskDialog so the user can copy the dump path to the clipboard.
        constexpr int ID_COPY_PATH = 100;
        const TASKDIALOG_BUTTON buttons[] =
        {
            { IDOK, L"OK" },
            { ID_COPY_PATH, L"Copy Path" },
        };

        const std::wstring content =
            L"The window has not processed any messages for " +
            std::to_wstring(stalledMs / 1000) + L" seconds.\n\n"
            L"A diagnostic dump was written to:\n" + path +
            L"\n\nPlease attach this file when reporting the hang.";

        TASKDIALOGCONFIG tdc = { sizeof(tdc) };
        tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        tdc.pszWindowTitle = L"WinDirStat diagnostics";
        tdc.pszMainIcon = TD_WARNING_ICON;
        tdc.pszMainInstruction = L"WinDirStat appears to have stopped responding.";
        tdc.pszContent = content.c_str();
        tdc.cButtons = _countof(buttons);
        tdc.pButtons = buttons;
        tdc.nDefaultButton = IDOK;

        int clicked = 0;
        if (SUCCEEDED(::TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)))
        {
            if (clicked == ID_COPY_PATH)
            {
                CopyTextToClipboard(path);
            }
        }
        else
        {
            // TaskDialog unavailable; fall back to plain MessageBox.
            const std::wstring message =
                L"WinDirStat appears to have stopped responding.\n\n"
                L"The window has not processed any messages for " +
                std::to_wstring(stalledMs / 1000) + L" seconds.\n\n"
                L"A diagnostic dump was written to:\n" + path +
                L"\n\nPlease attach this file when reporting the hang.";
            ::MessageBox(nullptr, message.c_str(), L"WinDirStat diagnostics",
                MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        }
    }

    // ---- watchdog thread -------------------------------------------------

    DWORD WINAPI WatchdogProc(LPVOID)
    {
        const unsigned thresholdMs = GetThresholdSeconds() * 1000u;
        constexpr DWORD pollIntervalMs = 1000;   // how often we ping the UI thread
        constexpr DWORD pingTimeoutMs = 500;     // per-ping wait for a response

        ULONGLONG hangStartTick = 0;             // 0 == UI currently responsive
        bool dumpedThisEpisode = false;

        // Wake every pollIntervalMs; exit promptly when the stop event is set.
        while (::WaitForSingleObject(g_stopEvent, pollIntervalMs) == WAIT_TIMEOUT)
        {
            const HWND hwnd = g_hwndToMonitor.load();
            if (hwnd == nullptr || !::IsWindow(hwnd)) continue;

            if (IsUiResponsive(pingTimeoutMs))
            {
                // Responsive again; reset the episode so a later hang re-dumps.
                hangStartTick = 0;
                dumpedThisEpisode = false;
                continue;
            }

            const ULONGLONG now = ::GetTickCount64();
            if (hangStartTick == 0)
            {
                hangStartTick = now;
            }
            else if (!dumpedThisEpisode && now - hangStartTick >= thresholdMs)
            {
                // One dump per hang episode: capturing repeatedly while still
                // wedged would just pile up huge files.
                const ULONGLONG stalledMs = now - hangStartTick;
                const std::wstring path = HangDump::WriteDump(L"hang");
                dumpedThisEpisode = true;
                if (path.empty()) continue;

                // Distinguish a stall from a hang before saying anything. Writing
                // the dump takes a moment, and by the time it lands the window has
                // often started responding again - the usual cause being that the
                // whole desktop was waiting on a saturated disk, not that
                // WinDirStat is wedged. Announcing "WinDirStat has stopped
                // responding" in a topmost modal dialog *after* it has resumed
                // reads like a crash report for a program that is working fine,
                // and the dialog is itself more disruptive than the stall was. So
                // keep the dump (it is what diagnoses the stall) but only speak up
                // while the window is genuinely still wedged, which is when the
                // user is actually staring at a frozen window and wants to know
                // why. WDS_HANGDUMP_NOTIFY_ALWAYS=1 restores the old behaviour for
                // anyone deliberately hunting transient stalls.
                if (!EnvEnabled(L"WDS_HANGDUMP_NOTIFY_ALWAYS") &&
                    IsUiResponsive(pingTimeoutMs))
                {
                    VTRACE(L"HangDump: UI stalled {} ms then recovered; dump kept at {}",
                        stalledMs, path);
                    continue;
                }

                NotifyDumpWritten(path, stalledMs);
            }
        }

        return 0;
    }

    LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info)
    {
        HangDump::WriteDump(L"crash", info);

        // Chain to any previously installed filter (and ultimately WER) so
        // normal crash reporting still happens.
        if (g_previousFilter != nullptr) return g_previousFilter(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

namespace HangDump
{
    std::wstring WriteDump(const wchar_t* reason, EXCEPTION_POINTERS* exceptionInfo)
    {
        // Guard against two triggers firing at once (e.g. a crash during a hang).
        std::scoped_lock lock(g_dumpMutex);
        if (g_dumpInProgress.exchange(true))
        {
            return {};
        }
        struct ResetGuard { ~ResetGuard() { g_dumpInProgress = false; } } resetGuard;

        const std::wstring directory = GetDumpDirectory();
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);

        SYSTEMTIME st;
        ::GetLocalTime(&st);
        wchar_t name[MAX_PATH];
        (void)swprintf_s(name, L"%s\\WinDirStat-%s-%lu-%04u%02u%02u-%02u%02u%02u.dmp",
            directory.c_str(), reason ? reason : L"dump", ::GetCurrentProcessId(),
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        const HANDLE file = ::CreateFile(name, GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        // Default dump: every thread's stack, the memory their stacks point at,
        // handle table (reveals named-object / lock ownership) and module lists.
        // This is normally only a few MB yet enough to diagnose a deadlock.
        // WDS_HANGDUMP_FULL=1 upgrades to a complete heap dump when needed.
        MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithProcessThreadData |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithHandleData |
            MiniDumpWithUnloadedModules);
        if (EnvEnabled(L"WDS_HANGDUMP_FULL"))
        {
            dumpType = static_cast<MINIDUMP_TYPE>(dumpType | MiniDumpWithFullMemory);
        }

        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        MINIDUMP_EXCEPTION_INFORMATION* meiPtr = nullptr;
        if (exceptionInfo != nullptr)
        {
            mei.ThreadId = ::GetCurrentThreadId();
            mei.ExceptionPointers = exceptionInfo;
            mei.ClientPointers = FALSE;
            meiPtr = &mei;
        }

        const BOOL written = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
            file, dumpType, meiPtr, nullptr, nullptr);
        ::CloseHandle(file);

        if (!written)
        {
            ::DeleteFile(name);
            return {};
        }

        VTRACE(L"HangDump wrote {} dump to {}", reason ? reason : L"", name);
        return name;
    }

    void Install(HWND hwndToMonitor)
    {
        if (EnvDisabled(L"WDS_HANGDUMP")) return;

        // Crash dumps work even without a window; install the filter regardless.
        g_previousFilter = ::SetUnhandledExceptionFilter(UnhandledExceptionHandler);

        g_hwndToMonitor.store(hwndToMonitor);
        if (hwndToMonitor == nullptr) return;

        if (g_watchdogThread != nullptr) return; // already running

        g_stopEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (g_stopEvent == nullptr) return;

        g_watchdogThread = ::CreateThread(nullptr, 0, WatchdogProc, nullptr, 0, nullptr);
        if (g_watchdogThread == nullptr)
        {
            ::CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
    }

    void Uninstall()
    {
        if (g_watchdogThread != nullptr)
        {
            if (g_stopEvent != nullptr) ::SetEvent(g_stopEvent);
            ::WaitForSingleObject(g_watchdogThread, 3000);
            ::CloseHandle(g_watchdogThread);
            g_watchdogThread = nullptr;
        }
        if (g_stopEvent != nullptr)
        {
            ::CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        if (g_previousFilter != nullptr || g_hwndToMonitor.load() != nullptr)
        {
            ::SetUnhandledExceptionFilter(g_previousFilter);
            g_previousFilter = nullptr;
        }
        g_hwndToMonitor.store(nullptr);
    }
}
