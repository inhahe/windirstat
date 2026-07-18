# Known Issues

## Hangs

### Hang during find-duplicates scan
- **Reported**: 2026-07-14
- **Dump**: `windirstat-hang-25452-20260714-131900.dmp` (in `%TEMP%\WinDirStat`)
- **Reproduction**: Occurred during a scan. The watchdog thread detected the UI thread stopped responding and wrote a diagnostic dump.

### Hang on exit
- **Reported**: 2026-07-14
- **Dump**: `windirstat-hang-25452-20260714-144525.dmp`
- **Reproduction**: Occurred when trying to exit WinDirStat after a scan. Same PID as the first hang, suggesting it was the same session.
- **Note**: Both hangs sharing PID 25452 means they occurred in a single run of WinDirStat.
