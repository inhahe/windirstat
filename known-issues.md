# Known Issues

## Fixed

### Cross-thread SendMessage deadlock (scan hang + exit hang)
- **Reported**: 2026-07-14
- **Dumps**: `windirstat-hang-25452-20260714-131900.dmp`, `windirstat-hang-25452-20260714-144525.dmp`
- **Root cause**: `InvokeInMessageThread` used synchronous cross-thread `SendMessage` from worker threads to the UI thread. When the UI thread was busy inside its own message processing (e.g. timer-driven `SortItems()` on the list control), worker threads blocked in `SendMessage(WM_CALLBACKUI)`. During shutdown, `SuspendExecution` waited for all workers to become idle, but workers were stuck in `SendMessage` waiting for the UI thread — classic cross-thread `SendMessage` deadlock.
- **Fix**: Switched `InvokeInMessageThread` from `SendMessage` to `PostMessage` with heap-allocated callbacks. Fixed all `[&]`-capture lambdas to use value captures (dangling references with async delivery). Added shutdown drain of pending `WM_CALLBACKUI` messages. Moved `CMessageBoxDlg::Show()` in `ProcessDuplicate` to the UI thread to avoid cross-thread `DoModal`.
- **Fixed in**: commit after f7706c17
