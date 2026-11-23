# 🐹 Chiikawa Desktop Study Pet

<div align="center">
  <img src="assets/chiikawa_twirl.gif" alt="Chiikawa celebrating" width="180">
</div>

**I was supposed to be studying. So naturally, I built a desktop pet instead.**

I had a train to catch in a few hours, a pile of studying waiting for me, and somehow decided the most productive thing I could do was make Chiikawa run around my Windows desktop in native C++.

Yes ~ I built a study timer instead of actually studying.

So here's a tiny desktop companion that wanders around, does random animations, and occasionally reminds me that I should probably get back to work.

Built with **C++20**, **Qt 6**, and questionable time management.

---

### 📦 Download Chiikawa

**[⬇️ Download for Windows](https://github.com/nguyenj1863/Study-Pet/releases)**

Windows 10 / 11 · 64-bit · portable ZIP

1. Grab the latest ZIP from [Releases](https://github.com/nguyenj1863/Study-Pet/releases)  
2. Extract it  
3. Double-click `ChiikawaStudyPet.exe`

---

## What can this little guy do?

- Runs around your desktop while you're *trying* to be productive
- Randomly switches between cute GIF animations
- Changes animation when you click him
- Reveals a settings gear when you double-click
- Lets you drag him wherever you want
- Hosts a real study countdown (pause / resume / reset and all)
- Shows remaining time when you hover during a session
- Twirls and pops a tiny toast when the timer finishes
- Lives quietly in the Windows system tray

No Electron. No browser window. Just a small native Win32 pet that stays out of the way.

---

## Meet the animations

<div align="center">
  <img src="assets/chiikawa_skip.gif" alt="Skip" width="110">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_stomp.gif" alt="Stomp" width="110">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_twirl.gif" alt="Twirl" width="110">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_fire.gif" alt="Fire" width="110">
</div>

---

## How to use

- **Click**; change to another random animation
- **Double-click**; show the settings gear (then click the gear)
- **Drag**; move Chiikawa around the desktop
- **Hover** (while a timer is running); see time remaining
- **System tray**; show/hide, start/pause timer, open settings, exit

Closing settings does **not** quit the pet. Use the tray **Exit** item for that.

---

## Built with

- **C++20**
- **Qt 6 Widgets** (`QMovie`, `QTimer`, `QSystemTrayIcon`, `QSettings`)
- **CMake**
- **MSVC 2022**

I wanted something lightweight enough to leave running all day ~ so this is a real Windows desktop app, not a webview wrapped in a 150 MB runtime.

---

## Getting started (for geeks (─‿─))

Ordinary Windows users: skip this section and use the [download link](#-download-chiikawa) above.

If you want to build from source:

### Prerequisites

- Windows 10 / 11 (x64)
- Visual Studio 2022 or Build Tools (Desktop C++ workload)
- CMake 3.21+
- Qt 6 Widgets + Test (`msvc2022_64` kit)

Example Qt install with [aqt](https://github.com/miurahr/aqtinstall):

```powershell
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
```

### Build

From a Developer PowerShell / after `VsDevCmd.bat -arch=amd64`:

```powershell
# Configure (VS generator)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"

# Or Ninja, if the VS generator isn't ready yet
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"

# Debug / Release
cmake --build build --config Debug
cmake --build build --config Release

# Tests
ctest --test-dir build -C Release --output-on-failure

# Run (VS multi-config)
.\build\Release\ChiikawaStudyPet.exe

# Run (Ninja single-config)
.\build\ChiikawaStudyPet.exe
```

Helper scripts:

```powershell
.\scripts\build.ps1 -Config Release -QtPrefix "C:\Qt\6.8.3\msvc2022_64"
.\scripts\build.ps1 -Config Release -Deploy -QtPrefix "C:\Qt\6.8.3\msvc2022_64"
.\scripts\deploy.ps1 -QtPrefix "C:\Qt\6.8.3\msvc2022_64"
.\scripts\package.ps1 -QtPrefix "C:\Qt\6.8.3\msvc2022_64"
```

### Packaging a portable ZIP

```powershell
.\scripts\package.ps1 -QtPrefix "C:\Qt\6.8.3\msvc2022_64"
# → dist\ChiikawaStudyPet-Windows-x64.zip
```

That ZIP is what GitHub Releases ships ~ exe + Qt runtime, no Visual Studio / Qt install required for end users.

---

## How it works (for geeks (─‿─))

| Piece | Job |
|-------|-----|
| `PetWindow` | Frameless translucent pet shell; mouse + orchestration |
| `AnimationManager` | Loads the four GIFs once, switches without reloading |
| `MovementController` | Idle/move sessions along the lower desktop band |
| `StudyTimer` | Accurate countdown via `steady_clock` (not a naive tick counter) |
| `TimerBubble` | Hover countdown; never steals focus |
| `CompletionToast` | Cute completion card next to the pet |
| `SettingsPanel` | Timer + pet preferences |
| `SystemTrayManager` | Tray menu + balloon notify |
| `AppSettings` / `StartupManager` | Persistence + optional HKCU startup |

**Timer state machine:** `Idle` → `Running` ⇄ `Paused` → `Completed`  
Reset/cancel return to idle. Completion emits **once** per run.

After Windows sleep: a **running** timer advances by real elapsed wake time; a **paused** timer stays frozen.

---

## Troubleshooting

- **CMake can't find Qt6**; pass `-DCMAKE_PREFIX_PATH` to your `msvc2022_64` kit
- **Missing DLLs at runtime**; run `windeployqt` on the exe
- **GIF won't load**; confirm `resources.qrc` embeds `assets/` and rebuild
- **Black box / no transparency**; frameless + `WA_TranslucentBackground`; update GPU drivers
- **`cl` not found**; open an x64 Native Tools prompt or run `VsDevCmd.bat -arch=amd64`
- **Startup toggle weirdness**; prefer a deployed exe path, not a temporary `build\` binary

---

## Closing note

If you're reading this instead of studying: same.

At least now Chiikawa can keep you company while you pretend the next Pomodoro will be different.
