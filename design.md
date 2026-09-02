# WinDirStat (fork) — design

Architecture and feature reference for this fork of WinDirStat 2.x. `README.md` is the
user-facing document (what the program does, and what this fork adds); this file is the
implementer's map (how it is put together, and which invariants must not be broken).
Keep both current when features or architecture change.

Related documents:

| Document | Role |
|---|---|
| `README.md` | User-facing: features, downloads, fork enhancements. |
| `CHANGELOG.md` | Release history. |
| `known-issues.md` | Reported crashes/hangs with root cause and fix status. |

---

## 1. What it is

An MFC (Visual Studio 2022, C++20, `/std:c++latest`) Win32 desktop application that scans
drives or folders and reports where disk space has gone: a sortable tree, an extension
breakdown, a treemap (or flame graph), duplicate detection, permissions, and a set of
cleanup actions.

Single project, `windirstat/windirstat.vcxproj`, producing `WinDirStat_<arch>.exe` for
x64/ARM64/x86. There is no separate library or test binary; `tests/` holds PowerShell
tooling (PGO training profiles, inlining/size reports), and `setup/` holds MSI/MSIX
packaging.

### Build

* `build.cmd` (repo) — full multi-arch/signing/MSIX pipeline.
* `..\build.bat` (one level above the repo) — local convenience build: builds the x64
  solution plus the x64 MSI and copies `exe`/`pdb`/`msi` next to itself.
* Pre-build steps (`ItemDefinitionGroup/PreBuildEvent`, see §9):
  * `windirstat/Build/Format Sources.ps1` — normalises every `.cpp`/`.h`/`.ps1` to CRLF,
    UTF-8 with BOM, no trailing whitespace, at most one blank line, single trailing
    newline. Only rewrites files whose bytes actually change.
  * `windirstat/Build/Compress Strings.ps1` — merges `res/langs/lang_*.txt` into
    `lang_combined.bin` (LZX cab), and regenerates `res/LangStrings.h` from the union of
    all keys. **String IDs are therefore generated, not hand-maintained**: add a line to
    `res/langs/lang_en.txt` and the `IDS_*` constant appears after the next build.
  * `windirstat/Build/GitRevision.targets` — stamps `GIT_COMMIT`, `GIT_DATE`, `GIT_COUNT`
    preprocessor defines from the enclosing git work tree (falls back to `DEADBEEF`/0).
* Compilation is parallel (`MultiProcessorCompilation`); the link uses whole-program
  optimisation, so a full rebuild is compile-parallel but link-serial.

---

## 2. Layout

```
windirstat/
  WinDirStat.cpp/.h          CDirStatApp — the CWinAppEx: startup, command line,
                             elevation, single-instance/CSV modes, shutdown ordering
  MainFrame.cpp              CMainFrame — window creation, splitters, timers, status bar,
                             progress, taskbar integration, dark-mode plumbing
  MainFrame.Commands.cpp     Command handlers + status-bar panes and the scan-mode chip
  MainFrame.Ribbon.cpp       Office-style ribbon construction
  WinDirStatModel.cpp        CWinDirStatModel — owns the item tree; scan lifecycle
  WinDirStatModel.Actions.cpp  Scanning engine, cleanup/refresh commands, central CmdUI
  Item.cpp / Item.Extended.cpp / Item.h   CItem — one node of the scanned tree
  ItemDupe/ItemTop/ItemSearch/ItemPerm    Row types for the non-tree list views
  Finder.h                   Abstract directory-enumeration interface
  FinderBasic.cpp            FindFirstFile/NtQueryDirectoryFile implementation
  FinderNtfs.cpp             Direct $MFT reader (fast path, needs elevation)
  UsnJournal.cpp             NTFS change-journal reader (hash-cache invalidation)
  HashCache.cpp              Durable cross-session file-hash cache
  Filtering.cpp              Compiled include/exclude/size filters
  CsvLoader.cpp              Scan save/load (CSV/JSON), duplicate export
  Options.cpp/.h             Typed, persisted settings (registry or portable ini)
  Localization.cpp           Runtime string lookup from the compressed language blob
  DarkMode.cpp               Dark palette, CDarkModeVisualManager, control subclassing
  HangDump.cpp               UI-thread watchdog + minidump writer
  Layout.cpp / LayoutPopup   Dialog auto-layout helpers and the layout picker popup
  Controls/                  Owner-drawn list/tree controls, treemap, flame graph, icons
  Views/                     CView wrappers that host the controls, plus the tab host
  Dialogs/  Pages/           Modal dialogs and the settings property pages
  res/                       Resources; res/langs/*.txt are the translation sources
```

---

## 3. Runtime structure

### Singletons

Most major objects are singletons reached through a static `Get()`:
`CDirStatApp`, `CMainFrame`, `CWinDirStatModel`, and each of the list controls
(`CFileTreeControl`, `CFileTopControl`, `CFileDupeControl`, `CFileSearchControl`,
`CFileWatcherControl`, `CFilePermsControl`). They are created during frame construction and
torn down in `CDirStatApp::ExitInstance`. Code that runs during startup or shutdown must
null-check them — several already do.

### Window hierarchy

```
CMainFrame  (CFrameWndEx)
├── CMFCRibbonBar        (when COptions::UseRibbon) — else CMFCToolBar + classic menu
├── m_splitter           vertical: upper panes | graph pane
│   ├── m_subSplitter    horizontal: file panes | extension list
│   │   ├── CFileTabbedView   tab host (see below)
│   │   └── CExtensionView    extension/type breakdown
│   └── CTreeMapView or CFlameGraphView   (COptions::UseFlameGraph selects which)
└── CWdsStatusBar        scan-mode chip | idle text (stretchy) | size | memory
```

`CFileTabbedView` owns a `CMFCTabCtrl` with a fixed set of panes, some of which are hidden
until relevant: **All Files**, **Largest Files**, **Duplicate Files** (only in duplicate
mode with results), **Search Results**, **Watcher**, **Permissions**, **Storage
Analytics**. Visibility is re-derived in `ResetOptionalTabVisibility()`, which runs on
`MODEL_CHANGE_NEW_ROOT`.

### Model change notification

`CWinDirStatModel::NotifyPanes(MODEL_CHANGE, CItem*)` fans out to every
`CWinDirStatPane::OnUpdate`. The change kinds (`MODEL_CHANGE_NEW_ROOT`,
`_SELECTION`, `_ZOOM`, `_LIST_STYLE`, `_TREEMAP_STYLE`, `_SIZE_MODE`, …) let panes do the
cheapest correct refresh. `MODEL_CHANGE_NEW_ROOT` is the heavyweight one: it rebuilds tab
visibility and resets the views.

### Status bar

`CWdsStatusBar` (in `MainFrame.h` / `MainFrame.Commands.cpp`) is a `CMFCStatusBar` with one
interactive pane. Panes, left to right:

| Index | Indicator | Content |
|---|---|---|
| 0 | `ID_INDICATOR_MODE` | Scan-mode drop-down chip (`Mode: Normal` / `Mode: Duplicates`) |
| 1 | `ID_INDICATOR_IDLE` | `SBPS_STRETCH`; selection/scan text, and hosts the progress bar |
| 2 | `ID_INDICATOR_SIZE` | Summed size of the selection |
| 3 | `ID_INDICATOR_RAM` | Process memory usage |

Two MFC behaviours matter here:

* **Every indicator needs an `ON_UPDATE_COMMAND_UI` handler.** With no handler, MFC's idle
  pass stamps `SBPS_DISABLED` on the pane and the visual manager paints its text in the
  *disabled* colour. All four indicators are registered against
  `CWinDirStatModel::OnUpdateCentralHandler`.
* The drop-down pane is drawn by an `OnDrawPane` override (rounded frame, chevron, hover
  and pressed states, hand cursor, tooltip). Its label colour is taken from a
  *non-disabled neighbouring pane* through the same `CMFCVisualManager::
  GetStatusBarPaneTextColor` call MFC uses, so it always matches the text beside it in
  either theme. Its width is recomputed on every update with the status bar's own font,
  because the first update runs from `OnCreate` before the frame has a monitor and
  `GetDpiForWindow` still answers 96.

---

## 4. The item tree

`CItem` is the single node type for the scanned tree. Type/flag bits (`ITEMTYPE`) encode
what a node is — `IT_MYCOMPUTER`, `IT_DRIVE`, `IT_DIRECTORY`, `IT_FILE`, plus synthetic
`IT_FREESPACE`, `IT_UNKNOWN`, `IT_HARDLINKS` — and `ITF_*` flags carry per-node state
(`ITF_ROOTITEM`, done/undone, reparse classification such as `ITRP_CLOUD`).

Sizes propagate upward as children complete, through the `Upward*` family
(`UpwardAddSizePhysical`, `UpwardSubtractFiles`, `UpwardSetUndone`, …). This is why a
partially scanned directory can already show provisional totals, and why removing a subtree
must subtract before unlinking.

Node identity for the NTFS path is the **file reference number** (`SetIndex`/`GetIndex`),
which lets the MFT reader reconcile placeholder items against authoritative records.

`CItemDupe`, `CItemTop`, `CItemSearch` and `CItemPerm` are separate row types for the
list-style views; they reference `CItem*` rather than owning the data. **They therefore
dangle if the tree is torn down without clearing them** — `CWinDirStatModel::ClearScanState`
exists precisely to do that in the right order.

---

## 5. Scanning pipeline

`CWinDirStatModel::StartScan(pathSpec)` → `ClearScanState()` → build the new root(s) →
`NotifyPanes(MODEL_CHANGE_NEW_ROOT)` → `StartScanningEngine({root})`.

`StartScanningEngine(items)` runs on the UI thread and then hands off:

1. Stop any previous engine (`StopScanningEngine`), stop the permissions scanner, and pull
   the items out of the duplicate/top/search lists.
2. Prune descendants of other queued items, and drop items that no longer exist or are now
   filtered out.
3. Recompile filters (`CFiltering::CompileFilters`).
4. Start **one `std::jthread`** which creates a `BlockingQueue<CItem*>` **per volume root**
   and `COptions::ScanningThreads` worker threads per queue. Each worker loops in
   `CItem::ScanItems`, popping directories and pushing discovered subdirectories back.
5. Wait for every queue to drain, then finalise: free-space/unknown nodes, hardlink
   adjustment, `ScanItemsFinalize`, `RebuildExtensionData`, optional quiet save.

`StopScanningEngine(StopReason)` distinguishes `Stop` (user pressed stop — keep results)
from `Abort` (a new scan or shutdown — the tree is about to be destroyed, so the UI must be
finalised before returning).

### Finders

`Finder` is the enumeration interface (`FindFile`/`FindNext` plus attribute/size/time/index
accessors). Two implementations:

* **`FinderBasic`** — `NtQueryDirectoryFile`/`FindFirstFile`. Always available, no
  privileges required. Also used for the immediate top-level expansion of a drive so the
  tree is not empty while the MFT is read.
* **`FinderNtfs`** — reads `$MFT` directly via `FSCTL_GET_RETRIEVAL_POINTERS` plus raw
  volume reads. Requires elevation and `COptions::UseFastScanEngine`. Builds two maps in
  `FinderNtfsContext::LoadRoot`:
  * `m_baseFileRecordMap`: file reference → size/time/attributes/reparse tag
  * `m_parentToChildMap`: parent reference → child names
  and then enumerates purely from memory.

> **Concurrency invariant in `LoadRoot`.** The MFT data runs are processed with
> `std::for_each(std::execution::par, …)`, so several threads share both maps. Each record
> is parsed into a **local** `FileRecordBase` and merged into `m_baseFileRecordMap` under
> `m_baseFileRecordMutex`; the map insertion and every write into the mapped value must
> happen inside that lock. Holding the lock only across `operator[]` and then mutating the
> shared node is a data race that corrupts the map — it was shipped once and crashed with
> an access violation inside `_Try_emplace` (see `known-issues.md`). Note that a base
> record and its extension records can live in different MFT extents, i.e. be parsed by
> different threads, under the same `baseRecordIndex`.

### Filtering

`CFiltering` compiles the user's include/exclude patterns and size cut-offs once per scan
(`CompileFilters`) and is then queried per item. Reparse-point handling is separate:
`CDirStatApp::IsFollowingAllowed(reparseTag)` decides whether a junction/symlink/cloud
placeholder is descended into.

---

## 6. Duplicate detection

Active only when `COptions::ScanForDuplicates`. Scan workers call
`CFileDupeControl::ProcessDuplicate(item, queue)` for each file.

1. **Size bucket first.** `m_sizeTracker` maps logical size → items. A file whose size is
   unique cannot have a duplicate, so nothing is hashed. (This is also stock behaviour.)
2. **Tiered hashing.** Once two files share a size they are hashed in escalating tiers —
   `ITHASH_SMALL` (first 4 KiB), `ITHASH_MEDIUM` (first 1 MiB), `ITHASH_LARGE` (whole
   file) — each tier only for the candidates that survived the previous one. The tier
   trackers (`m_trackerSmall/Medium/Large`) are `std::map<hash, items>` each with its own
   mutex; `m_nodeTracker`/`m_childTracker` build the display tree; `m_pendingListAdds`
   feeds rows to the UI thread.
3. **Cloud files are skipped** when `COptions::SkipDupeDetectionCloudLinks`, with a
   one-shot warning. That warning is raised through `InvokeInMessageThread` — a worker must
   never call `DoModal` with a UI-thread parent.
4. **`CHashCache`** persists every computed tier hash across sessions, keyed by path and
   validated against size + last-write time (both already read by the scan, so a hit costs
   no extra I/O). `UsnJournal` precisely invalidates entries for files changed, deleted or
   renamed since the cache was written. Saved scans (`CsvLoader`) also carry their hashes,
   so reloading one rebuilds the Duplicates view via `RebuildFromSavedHashes` without
   touching the disk.

### Switching scan mode

The status-bar chip calls `CMainFrame::SetScanMode(bool)`:

* **Turning duplicates off** needs no rescan — everything the other views show has already
  been collected. The Duplicates tab is hidden and the scan is left alone. The duplicate
  trackers are deliberately **not** cleared here, because workers may still be inside
  `ProcessDuplicate`; the next scan's `ClearScanState` frees them once the engine is idle.
* **Turning duplicates on** requires hashes, which only exist as a by-product of scanning,
  so the scan must restart. Because that discards the current results, it asks first.

---

## 7. Options, localization, theming

* **`COptions`** is a set of `Setting<T>` objects (`Options.h`) grouped by section. Each
  knows how to read/write itself through `PersistedSetting` (`Property.cpp`), which goes
  through MFC's profile API — the registry under `HKCU\Software\WinDirStat\WinDirStat`
  normally, or an `.ini` beside the executable in portable mode (detected by that file's
  existence, see `CDirStatApp` in `WinDirStat.cpp`). Settings are flushed during frame
  teardown, so a crash loses recent changes.
* **`Localization`** looks strings up by *name* (`IDS_*` as `std::wstring_view`), not by
  numeric resource id, from the compressed `lang_combined.bin` blob; missing translations
  fall back to English. `\n` in a language file is an escape, not a literal newline.
* **`DarkMode`** captures the system colours at startup, substitutes a dark palette, points
  MFC's `afxGlobalData` colours at it, installs `CDarkModeVisualManager`, and subclasses
  controls that do not theme themselves. Light mode uses
  `CMFCVisualManagerWindows7`. Anything that picks a colour should ask the visual manager
  or `DarkMode::WdsSysColor` rather than hard-coding a value, so both themes stay correct.

---

## 8. Threading rules

These are the invariants that have actually caused shipped bugs; treat them as binding.

* **`CMainFrame::InvokeInMessageThread` posts, it does not send.** It heap-allocates the
  callback and `PostMessage`s `WM_CALLBACKUI`. A synchronous cross-thread `SendMessage`
  from a scan worker deadlocks against the UI thread's own timer-driven `SortItems`.
  Consequences: lambdas passed to it must capture **by value** (a reference would dangle),
  and pending callbacks are drained in `OnClose` so they are not leaked.
* **Never `DoModal` from a worker thread** with a UI-thread parent — same deadlock. Route
  the dialog through `InvokeInMessageThread`.
* **Containers shared by scan workers need a lock for the whole update**, not just for the
  lookup that returns a reference into them (see the `LoadRoot` invariant in §5).
* **The UI thread must not tear down state a worker may still touch.** `ClearScanState`
  stops the engine *first*, then clears the controls, then deletes the tree — in that
  order.
* List sorting is throttled: `CMainFrame::OnTimer` re-sorts the visible list controls at
  most once per `LIST_SORT_INTERVAL_MS`, because `SortItems` is a full re-sort on the UI
  thread and running it every tick starves the message loop during a large scan.

---

## 9. Diagnostics

`HangDump::Install(hwnd)` runs once the main window exists, and `HangDump::Uninstall()` runs
at the *start* of `ExitInstance` so the watchdog cannot misfire on the deliberately
pump-less shutdown path. It installs two triggers:

* a watchdog thread that pings the main frame's message queue and writes a **hang** dump if
  the UI thread stops pumping — being a separate live thread, it can capture the stack of
  the wedged UI thread, which a hung process cannot do for itself;
* an unhandled-exception filter that writes a **crash** dump.

Dumps go to `%TEMP%\WinDirStat\` and are reported through a task dialog with a **Copy
Path** button. Tunable through the environment: `WDS_HANGDUMP=0` disables both triggers,
`WDS_HANGDUMP_SECONDS` sets the unresponsive threshold (default 15, minimum 3),
`WDS_HANGDUMP_DIR` redirects the output, `WDS_HANGDUMP_FULL=1` captures full memory, and
`WDS_HANGDUMP_SILENT=1` suppresses the notification.

`known-issues.md` records each reported dump with its root cause — add to it rather than
letting a diagnosis evaporate.

---

## 10. Fork-specific behaviour worth knowing

Full user-facing list is in `README.md` → *Fork enhancements*. The ones with architectural
weight:

* Ribbon interface by default, with a Windows 7 visual style in light mode and a flat
  dark-aware style in dark mode (`MainFrame.Ribbon.cpp`, `DarkMode.cpp`).
* Exact-byte sizes and sortable `YYYY-MM-DD HH:MM` timestamps by default; right-aligned
  numeric/timestamp columns are drawn in a fixed-width font and measured with that font so
  they are never clipped.
* Provisional values are shown for directories still being scanned, highlighted rather than
  replaced by a placeholder.
* The status-bar Pac-Man is replaced by a progress bar; the floating Pac-Man is off.
* Long MFT reads poll a cancellation callback so Stop and window-close respond promptly.
* Durable hash cache with USN-journal invalidation, and hashes persisted inside saved scans.
* Auto-elevation on startup is independent of the elevation prompt setting.
