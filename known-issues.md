# Known Issues

## Fixed

### Hang dialog for a stall the window recovered from
- **Reported**: 2026-09-02 ("windows said windirstat stopped responding ... the odd thing is that windirstat seems still to be running fine")
- **Dump**: `WinDirStat-hang-58216-20260902-214623.dmp`
- **What the dump shows**: the UI thread blocked in `DefWindowProc` forwarding `WM_SETCURSOR` (`rdx=0x20`, `lParam=0x02000001` = HTCLIENT from a mouse move) for the file-tree list view. Every scan worker was parked on its condition variable with ~0.1 s of CPU between them, so no scan was running and nothing was wedged in our own code. Thread 0 had 4.9 s user + **12.0 s kernel** time over 18.5 minutes of uptime.
- **Root cause**: not a WinDirStat defect. The machine was paging heavily (~670 pages/sec) against a 7200 RPM disk saturated by an unrelated build (queue depth ~7.5, 30-40 ms reads), so the UI thread missed its message pump for over 15 seconds. Windows logged no Application Hang event; the "stopped responding" wording the user saw was our own watchdog dialog.
- **Fix**: the watchdog still writes a dump on crossing the threshold, but now re-pings the window afterwards and stays silent if it is answering again - a stall the window recovers from is not worth a topmost modal dialog that reads like a crash report. The dump is kept regardless, and the notification, when shown, states how long the window was unresponsive. `WDS_HANGDUMP_NOTIFY_ALWAYS=1` restores the old behaviour.
- **Fixed in**: c1bbacf4

### Silent crash a couple of seconds after switching scan mode
- **Reported**: 2026-09-02 ("changed mode from duplicates to normal and a couple of seconds later, the window disappeared")
- **Dump**: `WinDirStat-crash-56136-20260902-115015.dmp` — access violation in `FinderNtfsContext::LoadRoot`'s `std::execution::par` lambda, inside `std::unordered_map::operator[]` → `_Try_emplace` → `_Find_last` (`rcx = 0`, i.e. the bucket vector was observed mid-reallocation).
- **Root cause**: a data race, not a scan-mode bug. `m_baseFileRecordMap` is a plain `std::unordered_map` shared by every worker of the parallel MFT loop. The code only held `m_baseFileRecordMutex` around the `operator[]` insertion and then mutated the returned `FileRecordBase` unlocked — so two threads could rehash/insert concurrently and corrupt the map. A base record and its extension records can live in different MFT extents (different threads) yet resolve to the same `baseRecordIndex`, so the writes race too. Switching scan mode merely made it easy to hit, because it restarted the scan.
- **Fix**: hold `m_baseFileRecordMutex` for the whole per-record update — the insertion *and* every write into that `FileRecordBase`. The parallelism that matters here is the overlapped MFT reads, not the record parsing.
- **Also changed**: switching *away* from duplicate mode no longer restarts the scan at all (nothing needs rehashing), which removes that restart as a trigger entirely.
- **Fixed in**: commit after 47547138

### Cross-thread SendMessage deadlock (scan hang + exit hang)
- **Reported**: 2026-07-14
- **Dumps**: `windirstat-hang-25452-20260714-131900.dmp`, `windirstat-hang-25452-20260714-144525.dmp`
- **Root cause**: `InvokeInMessageThread` used synchronous cross-thread `SendMessage` from worker threads to the UI thread. When the UI thread was busy inside its own message processing (e.g. timer-driven `SortItems()` on the list control), worker threads blocked in `SendMessage(WM_CALLBACKUI)`. During shutdown, `SuspendExecution` waited for all workers to become idle, but workers were stuck in `SendMessage` waiting for the UI thread — classic cross-thread `SendMessage` deadlock.
- **Fix**: Switched `InvokeInMessageThread` from `SendMessage` to `PostMessage` with heap-allocated callbacks. Fixed all `[&]`-capture lambdas to use value captures (dangling references with async delivery). Added shutdown drain of pending `WM_CALLBACKUI` messages. Moved `CMessageBoxDlg::Show()` in `ProcessDuplicate` to the UI thread to avoid cross-thread `DoModal`.
- **Fixed in**: commit after f7706c17
