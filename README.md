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

Don't want to compile C++ just to have a little character running around your desktop? Fair enough.

**[⬇️ Download Latest Release (v1.0.0)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/latest)**

#### Windows (64-bit)

- **Windows Installer (Recommended):** **[ChiikawaStudyPet-Setup-x64.exe](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.0.0/ChiikawaStudyPet-Setup-x64.exe)** (Windows 10 / 11 · per-user installer, no admin required)
- **Windows Portable (Advanced / optional):** **[ChiikawaStudyPet-Windows-x64.zip](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.0.0/ChiikawaStudyPet-Windows-x64.zip)** (portable ZIP, no install)

1. Download `ChiikawaStudyPet-Setup-x64.exe`  
2. Run the installer (installs **Chiikawa** to `%LOCALAPPDATA%\Programs\Chiikawa` and adds a Start Menu shortcut)  
3. Launch **Chiikawa** from the finish screen, Start Menu, or optional desktop shortcut  
4. On first launch, since the Windows build is not code-signed yet, Microsoft Defender SmartScreen may show *"Windows protected your PC"* — click **More info → Run anyway**

#### macOS (Apple Silicon & Intel)

- **Apple Silicon (M1 / M2 / M3 / M4):** **[ChiikawaStudyPet-macOS-arm64.dmg](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.0.0/ChiikawaStudyPet-macOS-arm64.dmg)**
- **Intel Mac (x64):** **[ChiikawaStudyPet-macOS-x64.dmg](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.0.0/ChiikawaStudyPet-macOS-x64.dmg)**

1. Download the DMG for your Mac  
2. Open it and drag `ChiikawaStudyPet.app` into **Applications**  
3. On first launch, since the macOS build is not notarized with an Apple Developer ID yet, macOS Gatekeeper may warn about an unidentified developer — use **Right-click → Open** (or **System Settings → Privacy & Security → Open Anyway**)

*Note: Desktop-Only Mode is currently supported on Windows only.*

---

## What can this little guy do?

- Runs around your desktop while you're *trying* to be productive
- Randomly switches between cute GIF animations
- Changes animation when you click him
- Reveals a settings gear when you double-click
- Lets you drag him wherever you want
- Desktop-Only Mode (Windows): keeps him behind your app windows, like a sticker on your wallpaper
- Hosts a real study countdown (pause / resume / reset and all)
- Shows remaining time when you hover during a session
- Twirls and pops a tiny toast when the timer finishes
- Lives quietly in the system tray (Windows) or menu bar (macOS)

No Electron. No browser window. Just a small native desktop pet that stays out of the way.

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

Ordinary users: skip this section and use the [download links](#-download-chiikawa) above.

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
