# WinDirStat - Windows Directory Statistics

## Description

WinDirStat is a disk usage analyzer and cleanup assistant for Microsoft Windows. It scans local drives, selected drives, individual folders, and command-line targets, then shows where storage is being used through a sortable directory tree, an extension/type breakdown, and an interactive [treemap](https://en.wikipedia.org/wiki/Treemap) where larger files and folders take up larger areas.

Alongside the visual overview, WinDirStat helps you investigate and act on disk usage. You can find large files, search and filter results, detect duplicates by hash, inspect logical versus physical size, watch file-system changes, save or reload scans, and launch cleanup or Windows maintenance actions directly from the interface.

For more information on the background of WinDirStat and alternative versions on other operating systems, please visit the [WinDirStat website](https://windirstat.net/)

### Major features

* Flexible scanning for local drives, selected drives, folders, and command-line targets, with refresh, suspend, resume, stop, fast NTFS scanning, multithreading, and elevated privilege support
* All Files, Largest Files, Duplicate Files, Search Results, File Watcher, Extension, and Treemap views
* Interactive treemap navigation with zooming, parent/child reselection, extension labels, logical or physical sizing, and configurable KDirStat or SequoiaView styling
* Sortable file details including logical/physical size, percentages, item counts, attributes, owner, modified time, free/unknown space, hardlinks, and hash prefixes
* Search, duplicate detection, and filtering with regular expressions, configurable hash algorithms, cloud-file safeguards, hardlink deduplication, path/name filters, size filters, and reparse-point exclusions
* File watching and reporting with created/deleted/modified/renamed events, CSV scan import/export, duplicate CSV export, and command-line CSV workflows
* Built-in actions for opening items, copying paths, selecting in Explorer, invoking the Explorer context menu, opening Command Prompt or PowerShell, moving files, showing properties, deleting files, and emptying folders or the Recycle Bin
* Windows cleanup and maintenance shortcuts for Disk Cleanup, Programs and Features, DISM, shadow copies, defrag, CHKDSK, VHDX optimization, hibernate files, user profiles, Mark-of-the-Web tags, sparse files, and NTFS compression
* User-defined cleanup actions plus dark mode, portable settings, Explorer context-menu integration, localization, locale-aware formatting, configurable columns, larger toolbar icons, and high-DPI aware UI behavior

For changes in recent versions, please check out [the change log](CHANGELOG.md).

### Installation

The recommended way to install WinDirStat is with a package manager, which also makes future updates easier:

* Install from the [Microsoft Store](https://apps.microsoft.com/detail/9ph1gl95p3wf) for Store-managed installation and updates
* Install with `winget install -e --id WinDirStat.WinDirStat` (or use `winget upgrade` later)
* Install with `choco install windirstat` (or use `choco upgrade windirstat` later)
* Install with `scoop install extras/windirstat` (requires `scoop bucket add extras`)

If you prefer a manual installer, need a portable archive, or want to browse older versions and beta builds, use the [GitHub releases page](https://github.com/windirstat/windirstat/releases/). If you are not sure which file to choose, download the **64-bit MSI installer**.

| Download | Best for | What is inside |
| --- | --- | --- |
| [Microsoft Store app](https://apps.microsoft.com/detail/9ph1gl95p3wf) | Users who want one-click installation and Store-managed updates | Installs WinDirStat through the Microsoft Store app experience on supported Windows systems. |
| [WinDirStat-x64.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-x64.msi) | Most users on modern Intel or AMD 64-bit Windows PCs | Standard Windows installer for 64-bit systems. Adds WinDirStat to the Start menu and installs it like a normal app. |
| [WinDirStat-arm64.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-arm64.msi) | Windows on ARM devices, including newer Surface devices and other Snapdragon-based laptops | Standard Windows installer built for ARM64 Windows. |
| [WinDirStat-x86.msi](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat-x86.msi) | Older 32-bit Windows installations | Standard Windows installer for 32-bit systems. |
| [MSIX bundle](https://github.com/windirstat/windirstat/releases/latest) | Windows App Installer or Store-style deployment across different CPU types | If the release includes an `.msixbundle` asset, it can contain packages for multiple CPU types and Windows chooses the right package for your device. |
| [WinDirStat.zip](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat.zip) | Portable use, testing, or running without an installer | Zip archive containing the WinDirStat executables. Extract it first, then run the executable for your CPU type. |
| [WinDirStat.7z](https://github.com/windirstat/windirstat/releases/latest/download/WinDirStat.7z) | Portable use when you already have 7-Zip installed | Same kind of portable executable archive as the zip file, usually with a smaller download size. |

## Fork enhancements

This fork extends the stock WinDirStat 2.x experience with a modernized interface, faster and more responsive scanning, and persistent duplicate-scan state. All of the behaviors below can be changed from **Options**; the defaults listed here are what a fresh install uses.

### Interface

* **Office-style tabbed ribbon (default).** The classic menu bar and toolbar are replaced by a tabbed ribbon that groups related commands into panels (File, Edit, Clean Up, View, Tools, Options, Help). Long command groups such as Clean Up collapse into drop-down buttons. You can switch back to the classic menu from **Options ▸ Ribbon Interface**.
* **Windows 7 visual style** for the ribbon, with command icons, an Office-style large/small button hierarchy, and check-box rendering for toggle commands.
* **Readable dark mode.** The ribbon uses flat, dark-aware styling so tab and command text is legible on dark backgrounds instead of black-on-grey.
* **Exact-byte sizes (default).** Sizes are shown as plain byte counts with no `KB`/`MB`/`GB` suffix and no trailing `" bytes"`. Re-enable rounded suffixes from **Options ▸ Use Size Suffixes**.
* **Sortable timestamps (default).** File times are formatted as `YYYY-MM-DD HH:MM PM`.
* **Corrected Percentage column.** The Percentage column now shows an actual percentage value instead of the malformed figure it displayed before.
* **Fixed-width stat columns.** Right-aligned numeric and timestamp columns are drawn in a monospaced font so digits line up, and are measured so their data is never clipped.
* **Columns auto-fit** the window and their content when a scan completes.
* **Cleaner size-proportion column.** Every proportion bar is drawn flush to the column's left edge (previously child rows were indented and narrower than the header row).
* **Scan-mode selector in the status bar.** The left end of the status bar shows the current scan mode (`Mode: Normal` or `Mode: Duplicates`) as a drop-down button — a framed chip with a chevron that highlights on hover — and clicking it opens a menu to switch modes. Switching *to* duplicate mode asks for confirmation first, because hashes are only produced while scanning and the scan must therefore start over; switching *away* from it keeps the current results and simply retires the Duplicates tab.

### Scanning and responsiveness

* **Immediate hierarchy.** The selected drive or folder expands right away instead of only after the MFT preload finishes, and selecting multiple drives no longer force-expands all of them.
* **Provisional values while scanning (default).** Directories whose totals are still being computed show their in-progress Percentage, Physical Size, Logical Size, and Files values highlighted in grey. Turning off **Highlight unfinished values** instead shows a `Working…` placeholder until each item is done.
* **No Pac-Man.** The floating Pac-Man animation is disabled by default, and the status-bar Pac-Man is replaced by a progress bar.
* **Cancellable scans.** Long NTFS MFT reads poll for cancellation, so Stop and window-close respond promptly, and shutdown is faster.
* **Thread-safe MFT parsing.** The parallel MFT reader serializes its shared record map, fixing a crash that could take the window down mid-scan on volumes whose file records span several MFT extents.

### Persistent duplicate scans

* **Durable hash cache.** Partial and full file hashes are stored across sessions, so a later duplicate scan can resume instead of rehashing everything. Hashes are also persisted inside saved scans, and the Duplicates view is rebuilt when such a scan is reloaded. As with stock WinDirStat, files are only hashed when two or more share the same size.
* **USN-journal cache invalidation.** On NTFS volumes the change journal is used to precisely drop cache entries for files that changed, were deleted, or were renamed since the cache was written, so resumed scans stay correct.

### Elevation

* **Auto-elevate on startup** works independently of the elevation prompt. Running elevated grants direct MFT access for faster NTFS scanning and enables privileged cleanup actions (for example creating volume shadow copies).

## Copyright / Licenses

* Copyright © WinDirStat Team ([windirstat.net](https://windirstat.net/))

The application itself is distributed under the terms of the [GPL v2](windirstat/res/license.txt), but parts of the source code are also available under more lenient license terms.

*Note:* you are not at liberty to upgrade the GPL version to anything later than v2 at this moment.

The logo and all derivatives are available under the terms of the Creative
Commons license [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

## Compatibility

WinDirStat 2.x has been developed for and tested on the following operating systems. They may work on older or newer operating systems but are not supported.

* Windows 7
* Windows 8
* Windows 8.1
* Windows 10
* Windows 11
* Windows Server 2008 R2
* Windows Server 2012
* Windows Server 2012 R2
* Windows Server 2016
* Windows Server 2019
* Windows Server 2022
* Windows Server 2025

## Resources

* A [website](https://windirstat.net/)
* A [blog](https://blog.windirstat.net/)
* Twitter/X as [@windirstat](https://x.com/windirstat)
* SubReddit [r/WinDirStat](https://www.reddit.com/r/WinDirStat/)

Find a more up-to-date list of resources on the website and the blog at any point in time.

## Official Downloads and Malware Warning

WinDirStat's popularity has led to unofficial websites that copy the project's name, branding, or downloads. These sites are not operated by the WinDirStat team, may offer outdated or modified files, and may expose users to malware.

For your safety, install WinDirStat only through the Microsoft Store link and package managers listed above, the official [GitHub releases](https://github.com/windirstat/windirstat/releases/), or links from [windirstat.net](https://windirstat.net/). The team reports impersonation sites when possible, but takedowns are not always successful.

## Building

WinDirStat can be built with Visual Studio 2022 or later. A Visual Studio solution file can be loaded from `windirstat\WinDirStat.sln`.

## Contributors

You can contribute by responding to issues, [developing](https://github.com/windirstat/windirstat/wiki/Developers) source code, or developing [translations](https://github.com/windirstat/windirstat/wiki/Contribute-Translation).

Thank you to everyone who has helped shape WinDirStat over the years.

<a href="https://github.com/windirstat/windirstat/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=windirstat/windirstat&max=1000" alt="WinDirStat contributor tiles" />
</a>

For additional historical contributors, testers, and translators, please check out [the contributors page](CONTRIBUTORS.md).

## Logo

![WinDirStat logo](windirstat/logos/logo_256px.png)
