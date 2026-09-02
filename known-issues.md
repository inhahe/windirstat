# Known Issues

## Fixed

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
