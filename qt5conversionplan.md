# qMetro: Qt 4 → Qt 5 Conversion Plan (Haiku / RenkuOS)

Status: phases 1–5 implemented on `converttoqt5` (see §11); phase 6 awaits a confirmed
CMake build.
Target: Qt 5.15.19 (HaikuPorts `qt5`, `qt5_devel`, `qt5_tools`, `qt5_script`, `qt5_script_devel`).
Branch: `converttoqt5`.

---

## 1. Current state of the tree

* Application: `qmetro` 0.7.1, a metro/subway map viewer and route finder.
* Build: qmake (`qmetro.pro` + 20 `*.pri` fragments). No CMake, no Makefile.
* Sources actually compiled today (from `rc/module/main.pri`): 17 `.cpp` files —
  `mainwindow`, `zip/zipglobal`, `zip/zip`, `zip/unzip`, `mapview/{mapview,maproute,mapgraphics,mapnode,mapedge,maplayer,mapelement}`,
  `completer`, `settings`, `details`, `ini/inisettings`, `mobileui`, `flickcharm`, `swipegesturerecognizer`,
  plus `src/main.cpp` (`rc/module/main_widget.pri`) and the update module
  (`src/update/{update,download}.cpp`, enabled by `rc/module/update.pri`).
* Forms: `ui/mainwindow.ui`, `ui/settings.ui`, `ui/details.ui` (all `version="4.0"`).
* Resources: `rc/resource.qrc` (icons, `locale/ru.qm`, `translit_ru`).
* Dead/side trees that must stay out of the build: `src/alarm/**` (QtMobility + libmodplug),
  `src/nfc/**` (QtMobility Connectivity), `src/qml/**` + `src/qmlmain.cpp` + `src/qmlviewer/**`
  (QtDeclarative; Maemo/MeeGo Harmattan only).
* The code already carries partial Qt5 guards (`QT_VERSION >= 0x050000` in `mobileui.h`,
  `flickcharm.cpp`, `completer.cpp`, `about.h`, `src/alarm/audiooutput.cpp`, `src/zlib/zconf.h`,
  and a `greaterThan(QT_MAJOR_VERSION, 4)` block in `rc/module/main.pri`), so it was clearly
  started as a dual-build project. Many Qt4-only constructs remain.

---

## 2. Blocking issues (must fix to compile)

These are hard errors under Qt 5.15, ordered by severity.

### 2.1 Build system drops QtNetwork although the update module needs it

`rc/module/main.pri` opens with:

```qmake
greaterThan(QT_MAJOR_VERSION, 4): {
 QT += widgets
 QT -= network        # <-- removes the module that update.pri already added
}
```

`qmetro.pro` includes `rc/module/update.pri` first (`QT += network`) and
`rc/module/main.pri` afterwards, so on Qt 5 the network module is removed again and
`src/update/*` fails with "QtNetwork: No such file or directory".
In CMake this becomes explicit and the bug disappears: link `Qt5::Network` whenever
`QMETRO_HAVE_UPDATE` is defined (it is, unconditionally, from `update.pri`).

### 2.2 `qApp->desktop()` without `<QDesktopWidget>`

`src/mainwindow.cpp` uses `qApp->desktop()->width()/height()/availableGeometry()` at lines
200, 323, 335 and 445, but includes only `<QDebug> <QSettings> <QFileDialog> <QKeyEvent>
<QMessageBox> <QTranslator> <QLocale> <QLibraryInfo> <QDir>` (lines 1–10).
In Qt 4 `<QtGui>` pulled the class in transitively; in Qt 5 `QDesktopWidget` lives in
**QtWidgets** and is not declared. This is a compile error (`invalid use of incomplete type`).

Fix (preferred, also removes a deprecated API): replace with `QScreen`:

```cpp
#include <QGuiApplication>
#include <QScreen>
static QRect screenGeometry()            { return QGuiApplication::primaryScreen()->geometry(); }
static QRect screenAvailableGeometry()   { return QGuiApplication::primaryScreen()->availableGeometry(); }
```

and rewrite the four call sites as `qMin(screenGeometry().width(), screenGeometry().height())`,
`screenAvailableGeometry().width()`, etc.
Fallback if a behavioural difference shows up on Haiku (multi-screen, Deskbar):
just add `#include <QDesktopWidget>` and keep `qApp->desktop()` — it still exists in Qt 5.15.

### 2.3 `QTextCodec::setCodecForCStrings` / `setCodecForTr` removed

`src/main.cpp:27-31` is already inside `#if QT_VERSION < 0x050000`, so it does not compile on
Qt 5 — no change needed. Action: leave the guard, but delete the now-unused local
`codec` variable warning path only if the compiler complains.

**Related runtime risk:** the whole app assumes a Windows-1251 codec:
`QTextCodec::codecForName("Windows-1251")` in `src/mainwindow.cpp:93` and
`src/mapview/mapview.cpp:137`, then `QSettings::setIniCodec(codePage)` (`mainwindow.cpp:158`,
`loadSettings()`), `INISettings(..., codePage)` and `QTextStream::setCodec` in `mapview.cpp`.
Qt 5 only always ships UTF-8/UTF-16/Latin-1; Windows-1251 comes from the optional codecs
plugin (present only when Qt was built with ICU or the `qcncodecs` plugin). If the lookup
returns `nullptr`, `setIniCodec(nullptr)` changes INI decoding and `INISettings`/`QTextStream`
will mis-decode or crash on `codePage->toUnicode()`.

Action (defensive, one small helper):

```cpp
static QTextCodec *mapCodec() {
    for (const char *n : {"Windows-1251", "CP1251", "windows-1251"})
        if (QTextCodec *c = QTextCodec::codecForName(n)) return c;
    qWarning() << "Windows-1251 codec unavailable, falling back to UTF-8";
    return QTextCodec::codecForName("UTF-8");   // never null
}
```

Use it in both places. If Russian map/locale text renders wrong after the build, the plan
fallback is to convert `bin/map/*.pmz`/INI assets once to UTF-8 (out of scope here, tracked as
a post-build contingency).

### 2.4 QtGui → QtWidgets split

Qt 5 moved widget classes out of `QtGui`. Remaining unguarded umbrella includes:

| File | Line | Include | Needs |
|---|---|---|---|
| `src/completer.h` | 4 | `#include <QtGui>` | `<QtWidgets>` (QLineEdit/QTreeWidget usage in subclasses) |
| `src/mobileui.cpp` | 2 | `#include <QtGui>` | `<QtWidgets>` |
| `src/mapview/mapview.cpp` | 18–29 | individual headers | all widget headers are included explicitly, OK |

Already correct: `src/mobileui.h:5`, `src/flickcharm.cpp:27`, `src/completer.cpp:3`,
`src/about.h:5` (`#if QT_VERSION >= 0x050000 → <QtWidgets>`).
Action: in `completer.h` and `mobileui.cpp` use the same 3-line `QT_VERSION` guard already used
in `mobileui.h` (keeps the file dual-buildable and matches project style).

### 2.5 qmake-only constructs with no Qt 5 meaning

* `CONFIG += mobility` / `MOBILITY = multimedia` (`rc/module/alarm.pri`) — QtMobility does not
  exist for Qt 5 and is not packaged on Haiku. The alarm module is already commented out in
  `qmetro.pro`, so it stays out of the first build (see §6).
* `QT += declarative` (`rc/module/main_quick.pri`) — module removed in Qt 5
  (`QtQuick`/`QtQml` replaced it). QML edition stays out of the build.
* `Q_WS_MAEMO_5`, `Q_WS_SIMULATOR`, `Q_WS_WIN` — the `Q_WS_*` macros were removed in Qt 5,
  so every block guarded by them is now silently dead code (`mainwindow.cpp` 82, 124–128,
  534, 971–974, 982, 1031, 1084–1087, 1501, 1525; `mapview.cpp` 82–90; `settings.cpp` 146;
  `completer.cpp` 129; `about.h` 52; `mobileui.cpp` 155; `qmlmain.cpp`).
  The symbols inside (`Qt::WA_Maemo5NonComposited`, `Qt::WA_Maemo5StackedWindow`,
  `Qt::WA_Maemo5AutoOrientation`) no longer exist either — they are only kept out of the
  build by the dead macro, which is fragile.
  Action: delete the Maemo/MeeGo branches (they can never be true on Qt 5 + Haiku) or, where
  deletion would churn too much, convert the guards to `#if 0` with a one-line comment.

### 2.6 `.pro`/`.pri` removal is required by the project rules

`AGENTS.md` mandates CMake and says the qmake files may be deleted once CMake is verified.
Deletion happens only **after** the CMake build is confirmed by the maintainer (§7, phase 6).

---

## 3. Deprecated-but-compiling APIs (fix in the same pass)

These compile on 5.15 but are removed in Qt 6 and/or emit warnings; they are cheap to change now
and they are the ones most likely to break on an unexpected Qt build.

| Where | Qt 4 idiom | Qt 5 replacement |
|---|---|---|
| `mapview.cpp:243, 1609` | `Qt::MidButton` | `Qt::MiddleButton` |
| `mapview.cpp:245, 253` | `event->delta()` | `event->angleDelta().y()` (and `pixelDelta()` where smooth scrolling matters) |
| `mapview.cpp:246-247` | `event->orientation()` | `qAbs(event->angleDelta().x()) > qAbs(event->angleDelta().y()) ? Qt::Horizontal : Qt::Vertical` |
| `mapview.cpp:894`, `mapnode.cpp:72` | `QMatrix` | `QTransform` (same `translate/rotate/map` API) |
| `mapview.cpp:262, 293` | commented `QMatrix` | leave, or modernise with the above |
| `completer.cpp:133-136` | `QTreeWidgetItem::setBackgroundColor/setTextColor` | `setBackground(col, QBrush)`, `setForeground(col, QBrush)` (data roles) |
| `mainwindow.cpp` (4 sites) | `qApp->desktop()` | `QGuiApplication::primaryScreen()` — see §2.2 |
| `mainwindow.cpp` | `QString::null`-era idioms, `QDesktopServices::storageLocation` if added later | `QStandardPaths::writableLocation` |

`mapview.cpp:150-151` and the rest use old-style `SIGNAL()/SLOT()` connects. Those are still
valid Qt 5; converting them to pointer-to-member syntax is **not** part of this conversion
(it is a large, risky, behaviour-preserving rewrite). Leave them.

---

## 4. Build system: qmake → CMake

One `CMakeLists.txt` in the project root, replacing `qmetro.pro` and the `.pri` fragments that
feed it (`rc/module/main.pri`, `rc/module/main_widget.pri`, `rc/module/update.pri`,
`rc/platform/nix.pri`). `rc/locale.pri`, `rc/module/main_quick.pri`, `rc/module/alarm.pri`,
`rc/module/nfc.pri`, `rc/platform/{win,macx,symbian,android}.pri` are dropped with their
source trees (§6).

### 4.1 Target CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(qmetro VERSION 0.7.1 LANGUAGES CXX)

# The runtime data folders live in a source-tree bin/, so an in-source build
# would stage over them and `make clean` would delete real project files.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Out-of-source build required: mkdir build && cd build && cmake ..")
endif()

# Haiku's qt5_devel installs the CMake config packages here; harmless elsewhere.
if(EXISTS /boot/system/lib/cmake)
  list(APPEND CMAKE_PREFIX_PATH /boot/system/lib/cmake)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(ZLIB REQUIRED)

set(QMETRO_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/settings.cpp
    src/details.cpp
    src/completer.cpp
    src/mobileui.cpp
    src/flickcharm.cpp
    src/swipegesturerecognizer.cpp
    src/ini/inisettings.cpp
    src/zip/zipglobal.cpp
    src/zip/zip.cpp
    src/zip/unzip.cpp
    src/mapview/mapview.cpp
    src/mapview/maproute.cpp
    src/mapview/mapgraphics.cpp
    src/mapview/mapnode.cpp
    src/mapview/mapedge.cpp
    src/mapview/maplayer.cpp
    src/mapview/mapelement.cpp
    # update module (QMETRO_HAVE_UPDATE)
    src/update/update.cpp
    src/update/download.cpp
)

set(QMETRO_HEADERS
    src/main.h
    src/mainwindow.h
    src/settings.h
    src/details.h
    src/about.h
    src/completer.h
    src/mobileui.h
    src/flickcharm.h
    src/swipegesturerecognizer.h
    src/ini/inisettings.h
    src/zip/zipglobal.h src/zip/zip.h src/zip/zip_p.h src/zip/zipentry_p.h
    src/zip/unzip.h src/zip/unzip_p.h
    src/mapview/mapview.h src/mapview/maproute.h src/mapview/mapgraphics.h
    src/mapview/mapnode.h src/mapview/mapedge.h src/mapview/maplayer.h src/mapview/mapelement.h
    src/update/update.h src/update/download.h
)

set(QMETRO_FORMS ui/mainwindow.ui ui/settings.ui ui/details.ui)

add_executable(qmetro ${QMETRO_SOURCES} ${QMETRO_HEADERS} ${QMETRO_FORMS}
               rc/resource.qrc)

target_include_directories(qmetro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The forms live in ui/, not beside the sources that include the generated
# headers (src/details.cpp includes "ui_details.h"), so AUTOUIC must be told
# where to find the matching .ui file.
set_target_properties(qmetro PROPERTIES
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_compile_definitions(qmetro PRIVATE
    APPNAME="qMetro"
    VERSION="0.7.1"
    QMETRO_PLATFORM="Source"
    QMETRO_HAVE_UPDATE=1)

target_link_libraries(qmetro PRIVATE
    Qt5::Widgets Qt5::Gui Qt5::Core Qt5::Network ZLIB::ZLIB)

# --- runtime layout ---------------------------------------------------------
# The executable and a staged copy of map/, locale/ and skin/ live together
# in <build>/bin, so nothing is written into the source tree and `make clean`
# removes the binary along with the staged data.
set(QMETRO_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bin)

set_target_properties(qmetro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${QMETRO_RUNTIME_DIR})

set(QMETRO_DATA_DIRS map locale skin)

set(QMETRO_STAGE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QMETRO_RUNTIME_DIR})
foreach(data_dir ${QMETRO_DATA_DIRS})
  list(APPEND QMETRO_STAGE_COMMANDS
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_CURRENT_SOURCE_DIR}/bin/${data_dir}
              ${QMETRO_RUNTIME_DIR}/${data_dir})
endforeach()

add_custom_target(stage_data ${QMETRO_STAGE_COMMANDS}
    COMMENT "Staging map/, locale/ and skin/ beside the executable")
add_dependencies(qmetro stage_data)

set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${QMETRO_RUNTIME_DIR})
```

Notes:
* `zip/zip.h` and `zip/unzip.h` include `"zlib.h"`, and that must resolve to the **system**
  header, not to `src/zlib`. Those two files are Qt's own copies of zlib 1.2.5, built for zlib
  compiled *inside* Qt Core: with Qt 5 `zconf.h` defines `Z_PREFIX`, which renames every zlib
  call to `z_*` (`z_crc32`, `z_deflateInit2_`, …), and it also redefines `ZEXTERN` as
  `Q_DECL_EXPORT`. Linked against the system `libz` (which exports the plain names) that yields
  undefined references at link time. `src/zlib` is therefore **not** on the include path and its
  headers are not in the source list; they are unused and can be deleted with the qmake files.
* The runtime layout is staged, not written into the source tree — see §4.2. It replaces
  qmake's `DESTDIR = bin`, because the program resolves `map/`, `locale/`, `skin/` relative to
  its own directory (`mainwindow.cpp:104`, `appPath=qApp->applicationDirPath()`), so the data
  folders must sit beside the binary wherever that is.
* `AUTOUIC` satisfies the `#include "ui_mainwindow.h"` etc. and puts the generated headers on the
  include path automatically. It resolves `ui_<name>.h` by looking for `<name>.ui` **next to the
  including source**, so the `AUTOUIC_SEARCH_PATHS` property pointing at `ui/` is required; without
  it `uic` stops with *"includes the uic file ui_details.h, but the user interface file details.ui
  could not be found in the following directories"*. The property needs CMake ≥ 3.9, well inside
  the 3.16 minimum declared at the top.
* `AUTOMOC` needs `main.h` listed (the `Q_OBJECT` class `TApplication`) and the headers that
  declare `Q_OBJECT`; the header list above is complete for that.
* `make clean` is generated by CMake (`cmake --build . --target clean`), satisfying `AGENTS.md`;
  `make install` is not added.
* Add `Makefile`-level convenience only if wanted; the documented build is
  `mkdir build && cd build && cmake .. && make`.
* If `find_package(Qt5 5.15 ...)` fails on the build machine, the exact component names are in
  `/boot/system/lib/cmake/Qt5*`; report the failure instead of loosening the version.

### 4.2 Runtime layout and `make clean`

qmake's `DESTDIR = bin` dropped the executable into the **source** `bin/` next to the shipped
`map/`, `locale/` and `skin/` data. That layout has a defect for a CMake build: the source tree
is not a build output, so `make clean` cannot remove the binary it left there, and an in-source
configure would stage data over the tracked files.

Adopted layout:

```
build/
  bin/
    qmetro            <- executable (RUNTIME_OUTPUT_DIRECTORY)
    map/ locale/ skin/  <- copies of the source data, refreshed by `stage_data`
    cache/            <- created at runtime by the app (appPath + "cache/")
```

* `stage_data` is an `ALL`-style custom target that `qmetro` depends on, so every `make`
  re-copies the three data folders (`cmake -E copy_directory`, a few small files). Editing
  `bin/skin/*.zip` or dropping in `bin/map/*.pmz` is picked up on the next build without
  touching C++ sources.
* `ADDITIONAL_CLEAN_FILES` adds `build/bin` (CMake ≥ 3.15, we require 3.16), so
  `make clean` removes the executable **and** the staged data. `rm -rf build` is the full reset.
* The in-source guard (`FATAL_ERROR` when `CMAKE_SOURCE_DIR == CMAKE_BINARY_DIR`) prevents a
  stray `cmake .` from turning the source `bin/` into the staging directory that `make clean`
  would then wipe.
* Consequence to be aware of: maps downloaded at runtime and settings written under
  `build/bin/` are build-tree state and disappear on `make clean`. The tracked data in the
  source `bin/` is never modified.
* To run an installed-style copy, `cp build/bin/qmetro bin/` (data is already there) — that is
  a manual step and deliberately not part of the build.

### 4.3 Things to verify in the generated build, not guessed

1. `rc/resource.qrc` paths are relative to `rc/` (`locale/ru.qm`, `img/*.png`, `translit_ru`) and
   all of those files exist in this tree — `rc/locale/ru.qm` and `rc/translit_ru` do.
2. `uic` 5 accepts `<ui version="4.0">` directly; only bump to `version="4.0"`+ manually if uic5
   rejects a specific widget (it should not).
3. Whether `Qt5::Multimedia` is present is irrelevant for phase 1 — the alarm module is out (§6).

---

## 5. Phased implementation

Each phase ends with a state the maintainer can build. Do not batch phases into one commit.

### Phase 0 — branch and baseline (already done)
`converttoqt5` created from the current HEAD; this plan committed.

### Phase 1 — CMake skeleton + minimal Qt5 source fixes
1. Add `CMakeLists.txt` (§4.1).
2. Fix §2.2 (`QScreen`/`QDesktopWidget` include in `mainwindow.cpp`).
3. Fix §2.4 (`QtWidgets` guards in `completer.h`, `mobileui.cpp`).
4. Do **not** touch API deprecations yet — the goal is a compiling 1:1 equivalent.

Acceptance: `mkdir build && cd build && cmake .. && make` completes; the executable and staged
`map/ locale/ skin/` land in `build/bin/`; the program starts from there and finds its data;
`make clean` removes the binary and the staged folders. Expect the first run here to surface
compile errors; they get fixed in the next commit but stay inside this phase.

### Phase 2 — Qt 4 → Qt 5 API cleanups (§3)
`Qt::MiddleButton`, `angleDelta()`, `QTransform`, `QTreeWidgetItem` data-role colours.
Behaviour must be identical: wheel zoom and middle-button pan keep the same step size
(`pow(2, delta/240)` becomes `pow(2, angleDelta().y()/240)`; one wheel notch is 120 in both).

### Phase 3 — dead mobile/desktop branches
Delete `Q_WS_MAEMO_5` / `MEEGO_EDITION_HARMATTAN` / `SYMBIAN` / `WINCE` / `ANDROID` branches
from the widget build (`mainwindow.cpp`, `mapview.cpp`, `settings.cpp`, `completer.cpp`,
`about.h`, `mobileui.cpp`, `main.h`, `download.cpp`, `zipglobal.cpp`). The Haiku desktop path is
the `#else` path in all of them, so this is deletion only, no new logic. Keep
`#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)` and the `Q_OS_HAIKU` block in
`mainwindow.cpp:106-121` intact.

### Phase 4 — Haiku path/data correctness
1. `mainwindow.cpp:106-121`: on Haiku only `appConfigFile` is set; `appMapPath` and
   `appCachePath` therefore fall back to `applicationDirPath()+"/map/"` and `"/cache/"`
   (lines 144–151). That works when running from `bin/`, but `AGENTS.md` forbids hardcoding
   system paths and prefers `find_directory()`. Decide one of:
   * keep the portable `applicationDirPath()` behaviour (no change, matches how the `bin/`
     data folders are laid out), or
   * use `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` for maps/cache and
     `QStandardPaths::ConfigLocation` for the INI — then seed the map/locale folders from
     `bin/` on first run.
   Recommended: **keep** `applicationDirPath()` for this conversion (it is what the shipped
   `bin/map`, `bin/locale`, `bin/skin` folders expect) and record the QStandardPaths idea as a
   follow-up. No change means nothing to break.
2. `rc/qmetro.desktop` has `Exec=/usr/bin/qmetro`; leave it, it describes an installed layout
   that `make install` does not provide yet.
3. `src/zip/zipglobal.cpp:29-41`: `OSDAB_ZIP_HAS_UTC` and `<utime.h>` are only enabled for
   Windows/Linux/macOS. Haiku has `utime()`; add `Q_OS_HAIKU` to both `#if` chains so zip
   timestamps are preserved. Small and self-contained.

### Phase 5 — codec hardening (§2.3)
Add the `mapCodec()` helper and use it at `mainwindow.cpp:93` and `mapview.cpp:137`; null-guard
the `QSettings::setIniCodec` and `INISettings`/`QTextStream::setCodec` call sites.

### Phase 6 — retire qmake (after maintainer confirms CMake build)
Delete `qmetro.pro`, `rc/module/*.pri`, `rc/platform/*.pri`, `rc/locale.pri`,
`src/nfc/nfcsettings/nfcsettings.pri`, `src/qmlviewer/qmlapplicationviewer.pri`.
`README` "Requirements" line is updated to Qt 5.15 and the CMake build command.
(Project rule: old qmake files may be deleted once CMake is verified.)

---

## 6. Modules deliberately left out of the first build

| Tree | Why | Decision |
|---|---|---|
| `src/alarm/**` + libmodplug | `CONFIG += mobility`, `MOBILITY = multimedia`, hardcoded `/usr/include/QtMultimediaKit` (alarm.pri:19); QtMobility does not exist for Qt 5 and is not packaged | exclude; `qmetro.pro` already comments it out. Porting later = QtMultimedia (`QAudioOutput`, already used in `audiooutput.cpp` with Qt5 guards) + drop mobility + CMake `find_package(Qt5 COMPONENTS Multimedia)` |
| `src/nfc/**` | QtMobility Connectivity (`QNearFieldManager`, `QNearFieldTagType2`) — no Qt 5 equivalent on Haiku | exclude; never referenced from the current build |
| `src/qml/**`, `src/qmlmain.cpp`, `src/qmlviewer/**` | QtDeclarative (`QDeclarativeView/Item/Context`) was replaced by QtQuick/QtQml in Qt 5; this code was Harmattan-only and also uses `QString::fromAscii` (removed) | exclude; do not port. A QtQuick port would be a separate project |
| `rc/platform/{win,macx,symbian,android}.pri`, `qtc_packaging/*` | platform packaging for the old targets | keep on disk, unused; delete with phase 6 if desired |

Consequence: `QMETRO_HAVE_ALARM` is not defined, so `about.h`'s player hooks stay compiled out —
verify `about.h` guards on `QMETRO_HAVE_ALARM` rather than on `QT_VERSION` (check while doing
phase 2).

---

## 7. Required packages

Already assumed present: `qt5`, `qt5_devel`, `qt5_tools`, `qt5_script`, `qt5_script_devel`.
Needed for this build and **not** in that list:

* `cmake` (and `cmake_devel` if the Haiku build of CMake splits it) — the build system is CMake.
* `zlib_devel` — `find_package(ZLIB)` + `-lz`. Its `zlib.h` is the one the build uses now; the
  configure step already reports the version it reads from it (`1.3.2` on Haiku).

No other packages are needed while the alarm/NFC/QML trees are excluded. If phase 6's optional
alarm port is picked up later, `qt5_multimedia`/`qt5_multimedia_devel` would be added then.

---

## 8. Risks and contingencies

1. **Qt 5.15 QtNetwork + TLS** for the update check (`src/update/download.cpp`): Qt 5 links
   OpenSSL dynamically. If the update check silently fails at runtime it is a TLS backend issue,
   not a port regression — check `QNetworkAccessManager` logs before changing code.
2. **Windows-1251 availability** (§2.3): if the codec is missing, Russian text from
   `bin/map/*.pmz` and the INI settings will be garbled. Contingency: ship/convert assets to
   UTF-8 in a separate change, never silently reinterpret.
3. **`QDesktopWidget` removal in a future Qt 6**: the §2.2 `QScreen` change avoids this.
4. **Touch/gesture code** (`QGestureEvent`, `QPinchGesture`, `FlickCharm`): compiles unchanged
   in Qt 5, but the actual gesture behaviour on Haiku is untested by design (no build here).
   Desktop mouse path is the primary target; gesture behaviour is flagged as "check after build".
5. **`TImageThreadWriter`** (`mapview.h:22-60`) moves a `QObject` to a `QThread` and deletes it
   from a timer; unchanged in Qt 5, but this is the most likely source of a one-off crash on
   export. If `saveToFile` crashes after the port, this class is the first suspect.
6. Dead `Q_WS_*` blocks are harmless at compile time but dangerous to leave: a future edit that
   re-enables one will fail on missing `Qt::WA_Maemo5*` constants. Phase 3 removes them.

---

## 9. Verification protocol

Builds are performed manually by the maintainer (`AGENTS.md`: do not attempt verification
builds in the conversion chat). Each phase is handed over with:

1. `mkdir build && cd build && cmake .. && make` — full configure + compile log.
2. `make clean` — confirms the clean target exists.
3. On success, a short smoke run from `build/bin/`: application starts, loads `bin/map/Moscow.pmz`,
   the search field finds a station, a route is drawn, zoom (wheel) and pan (middle drag) work,
   settings dialog opens and saves, and the language switch to `ru` still decodes correctly.
4. Qt 5 deprecation warnings are recorded verbatim (not suppressed) — they are the checklist for
   phase 2/3 cleanup.

Nothing in this plan assumes a successful build has happened.

---

## 10. Commit plan (branch `converttoqt5`)

| # | Content |
|---|---|
| 1 | `qt5conversionplan.md` (this document) |
| 2 | `CMakeLists.txt` + §2.2/§2.4 minimal compile fixes (phase 1) |
| 3 | API deprecations (`Qt::MiddleButton`, `angleDelta()`, `QTransform`, item colours) |
| 4 | dead `Q_WS_*`/Symbian/WinCE/Android/Maemo branches removed |
| 5 | codec hardening helper, Haiku zip `utime` support, path decision, README requirements line |
| 6 | delete `qmetro.pro` + `.pri` files (only after the CMake build is confirmed) |

Out of scope, listed as follow-ups: QtQuick edition, alarm/QtMultimedia port, NFC, converting
old-style `SIGNAL()/SLOT()` connects, `make install` + `.hpkg` recipe.

---

## 11. Implementation log

Implemented on `converttoqt5`, awaiting the maintainer's first CMake/Qt5 build:

| Phase | State | Notes |
|---|---|---|
| 1 — CMake + minimal fixes | done | `CMakeLists.txt`, `QScreen` helpers, `QtWidgets` guards in `completer.h`/`mobileui.cpp` |
| 2 — deprecated APIs | done | `Qt::MiddleButton`, `angleDelta()`, `QTransform` (mapview + mapnode), `QTreeWidgetItem::setBackground` |
| 3 — dead platform branches | done | `Q_WS_MAEMO_5`, `MEEGO_EDITION_HARMATTAN`, `Q_OS_SYMBIAN`, `Q_OS_ANDROID`, `Q_OS_WINCE` blocks removed from the built sources; `TApplication` is a plain `QApplication` now |
| 4 — Haiku specifics | done | `Q_OS_HAIKU` added to both `utime` conditions in `zipglobal.cpp`, `toTime_t()` → `toSecsSinceEpoch()`; path decision kept as `applicationDirPath()` (see §5 phase 4) |
| 5 — codec hardening | done | new `src/textcodec.h` (`mapCodec()` with UTF-8 fallback), used in `mainwindow.cpp` and `mapview.cpp` |
| 6 — retire qmake | **pending** | needs a confirmed `cmake .. && make` from the maintainer first; also deletes the now-unused `src/zlib/{zlib.h,zconf.h}` (see Report 4) |

Not compiled here: this workspace has no Qt 5 and no Haiku toolchain, so phases 1–5 are
unverified beyond configure/staging checks and source inspection.

### Build reports

**Report 1 — `AutoUic error`, `details.ui` not found.** The first maintainer build on Haiku reached
AUTOUIC and stopped:

```
"SRC:/src/details.cpp" includes the uic file "ui_details.h",
but the user interface file "details.ui" could not be found in the following directories
  "SRC:/src"
```

Cause: the three forms are kept in `ui/`, while the files that include the generated headers
(`src/details.cpp`, `src/settings.cpp`, `src/mainwindow.cpp`, `src/mobileui.cpp`) are in `src/`.
CMake's (configure-time) `AUTOUIC` only looks for `<name>.ui` beside the *including* source unless
the target sets `AUTOUIC_SEARCH_PATHS`; that property was missing from the phase-1 CMakeLists.txt.
Fix: `set_target_properties(qmetro PROPERTIES AUTOUIC_SEARCH_PATHS
${CMAKE_CURRENT_SOURCE_DIR}/ui)`.

Checked here with stubbed `moc`/`uic` (no Qt 5 in this workspace): the autogen step now passes
`ui/details.ui`, `ui/mainwindow.ui` and `ui/settings.ui` to `uic` and writes `ui_details.h`,
`ui_mainwindow.h`, `ui_settings.h`. Real compilation still has to be confirmed on Haiku.

**Report 2 — `src/zlib/zlib.h: No such file or directory`.** The build then failed while compiling
`qmetro_autogen/mocs_compilation.cpp`, inside `moc_zip_p.cpp` → `zip_p.h` → `zip.h:38`:

```
fatal error: src/zlib/zlib.h: No such file or directory
   38 | #include "src/zlib/zlib.h"
```

Cause: `zip/zip.h` included the vendored zlib header with a path relative to the **project root**.
That only ever resolved because qmake puts `-I.` (the source root) on the compiler command line;
the CMake target only had `-I<src>`, and an AUTOMOC-generated file lives in the build tree, so no
root-relative path resolves any more. `zip/unzip.h:37` already included it as plain `"zlib.h"`.

Fix: `src/zip/zip.h` now does `#include "zlib.h"`, and `CMakeLists.txt` adds
`${CMAKE_CURRENT_SOURCE_DIR}/src/zlib` to `target_include_directories`, mirroring qmake's
`INCLUDEPATH += src/zlib` (see `rc/module/main.pri:9`). No root-relative `#include "src/..."`
remains anywhere in `src/`.

Checked here: with `-I src -I src/zlib`, `"zlib.h"` resolves to the vendored `src/zlib/zlib.h` (the
probe stops only at its `#include <qglobal.h>`, i.e. Qt's headers, which are absent in this
workspace); the stub autogen run shows `-I<src/zlib>` on the target's compile flags and still
feeds all three forms to `uic`.

**Report 3 — `QPainterPath` is incomplete in `mapgraphics.cpp`.** The build got to 67% and stopped:

```
src/mapview/mapgraphics.cpp:163:48: error: return type 'class QPainterPath' is incomplete
src/mapview/mapgraphics.cpp:165:15: error: aggregate 'QPainterPath path' has incomplete type
```

Cause: `mapgraphics.h` declares `QPainterPath splinePath(...)`, `arrowPath(...)` and
`stairsPath(...)` but only included `<QPainter>`; Qt 4 pulled `QPainterPath` in through it, Qt 5
no longer does. Fix: `mapgraphics.h` includes `<QPainterPath>` itself. Every other file that uses
`QPainterPath` (`mapview`, `maproute`, `mapnode`, `mapedge`, `mapelement`) already has it, which is
why only this one translation unit failed.

The same build printed the Qt 5 deprecations below. They are warnings, not errors, but §5 phase 2
treats them as its checklist, so they are cleared here as well:

| File | Deprecated | Replaced with |
|---|---|---|
| `settings.cpp:36` | `QFontMetrics::width()` | `horizontalAdvance()` (Qt ≥ 5.11) |
| `zip.cpp:1024` | `qBinaryFind()` | `std::lower_bound()` + explicit equality test (`<algorithm>`) |
| `mapview.cpp:420,451,1732` | `QGraphicsView::matrix()` | `transform()` |
| `mapview.cpp:626,1089` | `QString::SkipEmptyParts` | `Qt::SkipEmptyParts` (Qt ≥ 5.14) |
| `mapview.cpp:2002` | `QList::swap(int,int)` | `swapItemsAt()` (Qt ≥ 5.13) |

Haiku's Qt 5.15.19 provides all of these. The `qBinaryFind` swap had to keep its exact semantics:
`lower_bound` stops at the insertion point, so `kw != end` alone would report a match for any
extension sorting before the first entry. A standalone harness run over the real `ext1`–`ext3`
tables confirms old and new agree for every entry, for upper-case variants, and for out-of-range
probes.

**Report 4 — link failure: `undefined reference to z_crc32`, `z_deflateInit2_`, …** Every
object compiled (96%) and the link died:

```
ld: CMakeFiles/qmetro.dir/src/zip/zip.cpp.o: undefined reference to `z_get_crc_table'
ld: ... undefined reference to `z_crc32'
ld: ... undefined reference to `z_deflateInit2_'
ld: CMakeFiles/qmetro.dir/src/zip/unzip.cpp.o: undefined reference to `z_inflateInit2_'
```

Cause: the `z_` prefix. `src/zlib/zlib.h` and `src/zlib/zconf.h` are Qt's own copies of zlib
1.2.5, shipped for zlib compiled *inside Qt Core*, and `zconf.h:11-14` is explicit about it:

```c
/* Since Qt Core must export these symbols, define Z_PREFIX to avoid clashes system zlib */
#include <qglobal.h>
#if QT_VERSION >= 0x050000
#define Z_PREFIX
#endif
```

With Qt 4 that guard was false and the plain symbol names were used, which is why the old qmake
build linked. Under Qt 5 `Z_PREFIX` turns every zlib call into a `z_*` name (`#define crc32
z_crc32`, `#define deflateInit2_ z_deflateInit2_`, …), and `ZLIB::ZLIB` is the system `libz`,
which exports only the plain names — `nm -D` on it shows `crc32`, `get_crc_table`,
`deflateInit2_`, `inflate` and no `z_crc32` at all. The header also redefines `ZEXTERN` as
`Q_DECL_EXPORT`, another sign it is meant for Qt's internal build.

Fix: link the system zlib and use **its** header. `src/zlib` came off the include path and both
vendored headers came out of the source list, so `#include "zlib.h"` resolves through
`ZLIB::ZLIB`'s include directories. Two consequences had to be handled:

* `zip_p.h` used `typedef uLongf crc_t`. `uLongf` is 64-bit on LP64, while current zlib declares
  `const z_crc_t *get_crc_table()` with a 32-bit entry type, so the assignment would no longer
  compile. It is now `typedef quint32 crc_t` and `zip.cpp` casts the result the way `unzip.cpp`
  already did (`unzip_p.h` has always kept the table as `const quint32*`). The CRC arithmetic is
  32-bit and `updateChecksum()` returns `quint32`, so no value changes.
* `MAX_WBITS` is **not** in zlib's public `zlib.h` (it lives in the private `zutil.h`); the
  Qt-bundled `zconf.h` happened to define it, and `zip.cpp:643`/`unzip.cpp:812` pass
  `-MAX_WBITS` for raw deflate. `zipglobal.h` now defines it as 15 under an `#ifndef` guard.

Checked here: a probe translation unit containing the exact expressions from `zip.cpp`,
`unzip.cpp` and `zip_p.h` (`get_crc_table()` into `const quint32*`, `inflateInit2_(... , -MAX_WBITS,
ZLIB_VERSION, sizeof(z_stream))`, `deflateInit2_`, `crc32`, and the `CRC32(c,b)` macro) compiles
with `-Wall -Wextra` against a real system `zlib.h` (1.3 here, 1.3.2 on Haiku) and links with
`-lz` with no `z_*` references. The configure step on Haiku reporting `found version "1.3.2"`
confirms `FindZLIB` located that same system header, so the include now resolves to it.

Observed but **not** changed (upstream OSDaB data, outside the Qt 5 port): `ext2` in `zip.cpp`
lists `"jpg"` before `"jpeg"`, so the table is not strictly ordered and the binary search can never
find `"jpeg"`. That is pre-existing — `qBinaryFind` missed it too, and the new code reproduces the
old result exactly. Fixing it means reordering the table and changes the compression level chosen
for `.jpeg` files, so it belongs in a separate change.
