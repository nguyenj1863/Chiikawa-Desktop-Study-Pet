<div align="center">

# 🐹 Chiikawa Desktop Study Pet

<img src="assets/chiikawa_twirl.gif" alt="Chiikawa celebrating" width="160">

**I was supposed to be studying. So naturally, I built a desktop pet instead.**

[![Release](https://img.shields.io/badge/release-v1.1.0-f4a5a5?style=flat-square)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/tag/v1.1.0)
[![Wiki](https://img.shields.io/badge/docs-GitHub%20Wiki-e89bb0?style=flat-square)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-6c8ebf?style=flat-square)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Building-from-Source)
[![Qt 6](https://img.shields.io/badge/Qt-6.8-41cd52?style=flat-square)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Application-Architecture)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS-8d7b68?style=flat-square)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Releases-and-Compatibility)
[![License](https://img.shields.io/badge/code%20license-MIT-d9a05b?style=flat-square)](LICENSE)

**[⬇️ Download Latest Release (v1.1.0)](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/latest)** · **[📖 Read the Wiki](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki)** · **[🐛 Report an Issue](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/issues)**

</div>

---

I had a train to catch in a few hours, but wanted to get some work in, so I made Chiikawa help out with tracking time, since the Microsoft alarm UI is ugly.

So here's a tiny native desktop companion that wanders around your screen, runs a study countdown timer, and rings one-time alarms when you need a reminder. Built with **C++20**, **Qt 6**, and questionable time management. No Electron, no browser window, and zero telemetry.

---

## 📦 Download

Don't want to compile C++ just to have a little character running around your desktop? Fair enough.

| Platform | Download | Notes |
|---|---|---|
| **Windows 10 / 11 (Installer)** | **[ChiikawaStudyPet-Setup-x64.exe](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.1.0/ChiikawaStudyPet-Setup-x64.exe)** | **Recommended.** Per-user install to `%LOCALAPPDATA%\Programs\Chiikawa` (no admin required) |
| **Windows 10 / 11 (Portable)** | **[ChiikawaStudyPet-Windows-x64.zip](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.1.0/ChiikawaStudyPet-Windows-x64.zip)** | Extract-and-run portable archive |
| **macOS 13+ (Apple Silicon)** | **[ChiikawaStudyPet-macOS-arm64.dmg](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.1.0/ChiikawaStudyPet-macOS-arm64.dmg)** | Drag `ChiikawaStudyPet.app` into Applications |
| **macOS 13+ (Intel x64)** | **[ChiikawaStudyPet-macOS-x64.dmg](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases/download/v1.1.0/ChiikawaStudyPet-macOS-x64.dmg)** | Drag `ChiikawaStudyPet.app` into Applications |

> [!NOTE]
> **Unsigned builds:** Releases are not yet signed with a paid Windows certificate or notarized with an Apple Developer ID. On Windows SmartScreen, click **More info → Run anyway**. On macOS Gatekeeper, open **System Settings → Privacy & Security → Open Anyway** after your first launch attempt. You can verify every download against `SHA256SUMS.txt` on the [Releases page](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/releases). For detailed steps, see the **[Installation Guide](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Installation)**.

---

## ✨ What Can This Little Guy Do?

- **Animated Desktop Companion:** Wanders along your desktop, randomly switches between 4 embedded GIF animations, and reacts when you click or drag him.
- **Study Countdown Timer:** Built-in `25m` / `45m` / `60m` presets, custom `HH:MM:SS` duration editor (`1 s` to `24 h`), monotonic `steady_clock` accuracy across sleep/wake, hover countdown pill, and a completion toast with **Start Again**.
- **One-Time Alarms & Snooze:** Local wall-clock alarms with `12h` / `24h` time selectors, `5m` / `10m` / `15m` snooze intervals, and a 15-minute wake/restart grace window.
- **Built-In Sounds:** Three synthesized alarm/timer WAV chimes (`Soft Chime`, `Tiny Bell`, `Cozy Pop`) with volume control, plus an independent UI button click sound.
- **Customizable & Unobtrusive:** Adjustable pet size (`64–256 px`), movement speed (`0.25×–3.0×`), synchronized pet + Settings dragging, interactive onboarding tutorial, optional launch on startup, and **Desktop-Only Mode** (Windows) to pin Chiikawa behind your open windows like a wallpaper sticker.
- **System Tray / Menu Bar & Offline Privacy:** Lives quietly in the Windows system tray or macOS menu bar, stores preferences strictly locally, and makes zero network requests.

<div align="center">
  <img src="assets/chiikawa_skip.gif" alt="Skip" width="96">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_stomp.gif" alt="Stomp" width="96">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_twirl.gif" alt="Twirl" width="96">
  &nbsp;&nbsp;
  <img src="assets/chiikawa_fire.gif" alt="Fire" width="96"><br>
  <sub>Built-in animations: <strong>Skip</strong>, <strong>Stomp</strong>, <strong>Twirl</strong>, and <strong>Fire</strong></sub>
</div>

---

## 🖱️ Quick Controls

| Action | What Happens |
|---|---|
| **Single-click** | Switches Chiikawa to another random animation |
| **Double-click** | Reveals the **settings gear** in the top-right corner for 5 seconds (click the gear to open Settings) |
| **Click and drag** | Moves Chiikawa freely across your screen (moves Settings together when open) |
| **Hover (timer active)** | Shows a floating countdown pill above Chiikawa |
| **Tray / Menu Bar** | Show/Hide Pet, Start/Pause/Resume Timer, Open Settings, Show Tutorial, or Exit |

> [!TIP]
> Closing the Settings popup (`×`) only hides Settings. To quit completely, click **Exit Application** at the bottom of Settings or choose **Exit** from the system tray / menu bar.

---

## 📖 Documentation (GitHub Wiki)

Need step-by-step setup help, visual demos, troubleshooting, or technical internals? Everything is documented in the **[Project Wiki](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki)**:

| User Guides | Developer & Project Docs |
|---|---|
| **[Installation](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Installation)** & **[Quick Start](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Quick-Start)** | **[Building from Source](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Building-from-Source)** (Windows & macOS) |
| **[Controls and Interactions](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Controls-and-Interactions)** | **[Application Architecture](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Application-Architecture)** (with Mermaid diagram) |
| **[Study Timer](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Study-Timer)** & **[Alarms and Snooze](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Alarms-and-Snooze)** | **[Development Guide](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Development-Guide)** & **[Testing](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Testing)** |
| **[Settings](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Settings-and-Customization)** & **[Notifications and Sounds](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Notifications-and-Sounds)** | **[Build and Release Pipeline](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Build-and-Release-Pipeline)** & **[Contributing](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Contributing)** |
| **[FAQ](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Frequently-Asked-Questions)** & **[Troubleshooting](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Troubleshooting)** | **[Compatibility](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Releases-and-Compatibility)** · **[Privacy](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Security-and-Privacy)** · **[Roadmap](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Known-Issues-and-Roadmap)** |

---

## 🛠️ Building from Source (for geeks (─‿─))

Built with **C++20**, **Qt 6** (`Widgets`, `Multimedia`, `Test`), and **CMake 3.21+**.

### Windows (MSVC 2022 + Ninja)
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### macOS (Apple Silicon or Intel, macOS 13+)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

For full prerequisites, packaging (`windeployqt` / Inno Setup / `macdeployqt` / DMG), architecture notes, and contributor guidelines, see **[Building from Source](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Building-from-Source)** and **[Application Architecture](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Application-Architecture)**.

---

## 📄 License & Attribution

- Original source code, scripts, tests, and synthesized WAV sounds are licensed under the **[MIT License](LICENSE)** (© 2026 John Nguyen).
- Third-party components (Qt 6 under LGPLv3, FFmpeg under LGPLv2.1+, Pixelify Sans & Nunito under SIL OFL 1.1) and the Chiikawa character artwork notice are documented in **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)** and **[Licensing and Attribution](https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet/wiki/Licensing-and-Attribution)**.

---

## Closing Note

If you're reading this instead of studying: same.

At least now Chiikawa can keep you company while you pretend the next Pomodoro will be different.
