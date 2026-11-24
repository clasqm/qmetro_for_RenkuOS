# qMetro Qt 4 → Qt 5 Conversion Report

Branch: `converttoqt5`. Target: Qt 5.15 on Haiku / RenkuOS.
Companion document: `qt5conversionplan.md` (the full plan, plus the section 11 implementation log).

## What the plan asked for

The plan covered six phases: replace the qmake build with CMake, fix the Qt 5 hard
errors (`QDesktopWidget`, the `QtGui`/`QtWidgets` split), clean up deprecated APIs,
delete the dead Maemo/Symbian/WinCE/Android branches, add Haiku-specific fixes, harden
the Windows-1251 codec lookup, and finally retire the `.pro`/`.pri` files once the
CMake build was confirmed.

## How it was executed

**Phases 1–5** landed as three commits: the CMake build plus the first compile fixes,
then the Qt 5 API cleanups, then the platform-branch removal with the Haiku `utime`
and codec work. The build system became a single root `CMakeLists.txt` that builds the
21 compiled sources, the three forms under `ui/`, the resource file and the update
module. The executable and a staged copy of `map/`, `locale/` and `skin/` go into
`build/bin/`, and `make clean` removes all of it through `ADDITIONAL_CLEAN_FILES`.
`QScreen` replaced `qApp->desktop()`, `Qt::MiddleButton` / `angleDelta()` /
`QTransform` / `setBackground` / `horizontalAdvance()` / `std::lower_bound()` replaced
their deprecated counterparts, and the gravity of `src/textcodec.h` became a
`mapCodec()` helper that falls back to UTF-8 instead of trusting that Windows-1251
exists.

**The build reports drove the rest.** The maintainer built after each hand-over and
sent back the error. Four were fixed in turn: `AUTOUIC_SEARCH_PATHS` pointing at `ui/`
so `uic` could find the forms; `zip.h` including `"zlib.h"` the way `unzip.h` already
did; `mapgraphics.h` including `<QPainterPath>` for itself; and the link failure caused
by Qt's bundled `zconf.h` defining `Z_PREFIX` under Qt 5 — that one dropped the vendored
zlib headers in favour of the system zlib, changed `crc_t` to `quint32` and supplied
the missing `MAX_WBITS`. Each deprecation warning in those logs was cleared in the same
pass.

With the CMake build confirmed to link, **phase 6** removed every `.pro` and `.pri`
file in the tree, the unused `src/zlib` headers, and rewrote the `README` requirements
block and the `lupdate`/`lrelease` recipes in `rc/locale/translation.txt` around the
CMake build.

**Then the fork was narrowed to its only targets.** The Debian, Maemo and Harmattan
packaging trees, the QtMobility alarm and NFC modules (including the bundled
libmodplug), the QtDeclarative QML edition, the macOS/Windows/WinCE/MeeGo integration
files and two unreferenced icons were deleted — 111 files. The guards that pointed at
those trees (`QMETRO_HAVE_ALARM`, `QMETRO_HAVE_VIBRO`, `QMETRO_HAVE_NFC`,
`QML_EDITION`) came out of the built sources with them; none of those macros was ever
defined by the CMake build, so compiled behaviour is unchanged. `QMETRO_HAVE_UPDATE`
remains.

## Verification and limits

Per `AGENTS.md`, all builds are manual and no build was attempted here — this workspace
has no Qt 5 and no Haiku toolchain. Verification was limited to stubbed `moc`/`uic`
configure and autogen runs, include-path and flag inspection, standalone zlib and
binary-search probes, and static checks (no dangling references to the removed trees,
balanced preprocessor blocks, every path in `CMakeLists.txt` present). Behavioural items
that only a real build can settle — gesture handling, the image export thread, runtime
TLS for the update check and the Windows-1251 rendering of the map data — are flagged in
section 8 of the plan.
