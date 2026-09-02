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

#include <string>

//
// Diagnostic crash / hang capture.
//
// HangDump writes a Windows minidump (.dmp) that can be opened in WinDbg/cdb or
// Visual Studio to inspect the call stacks of every thread at the moment the
// process misbehaved. It exists so that an intermittent "stopped responding"
// hang (observed during long find-duplicates runs) can be diagnosed after the
// fact instead of having to kill the process blind.
//
// Two triggers are installed by Install():
//   * A background watchdog thread that periodically pings the UI thread's
//     message queue. If the UI thread stops pumping messages for longer than
//     the configured threshold, the watchdog writes a "hang" dump. Because the
//     watchdog is a separate, still-running thread it can dump the stack of the
//     wedged UI thread (and every worker thread) that a hung process cannot
//     capture itself.
//   * An unhandled-exception filter that writes a "crash" dump on an otherwise
//     fatal exception.
//
// Behaviour is controlled by environment variables (all optional):
//   WDS_HANGDUMP           "0" disables the watchdog + crash filter entirely.
//   WDS_HANGDUMP_SECONDS   UI-unresponsive threshold before a hang dump is
//                          written (default 15, clamped to >= 3).
//   WDS_HANGDUMP_DIR       Directory to write dumps into (default %TEMP%\WinDirStat).
//   WDS_HANGDUMP_FULL      "1" captures a full-memory dump (large, but includes
//                          the entire heap). Default is a stacks + referenced
//                          memory + handle dump, which is enough to see a
//                          deadlock and stays small.
//   WDS_HANGDUMP_SILENT    "1" suppresses the "dump written" message box.
//
namespace HangDump
{
    // Installs the unhandled-exception filter and starts the UI-hang watchdog.
    // Call once, after the main window exists. hwndToMonitor is the window whose
    // responsiveness is watched (normally the main frame). Safe to call with a
    // null handle, in which case only the crash filter is installed.
    void Install(HWND hwndToMonitor);

    // Stops the watchdog thread and removes the exception filter. Call during
    // shutdown. Safe to call even if Install() was never called.
    void Uninstall();

    // Writes a minidump immediately. 'reason' is a short tag embedded in the
    // file name (e.g. L"hang", L"crash", L"manual"). When 'exceptionInfo' is
    // non-null it is recorded in the dump so the faulting context is preserved.
    // Returns the full path of the written dump, or an empty string on failure.
    std::wstring WriteDump(const wchar_t* reason, struct _EXCEPTION_POINTERS* exceptionInfo = nullptr);
}
