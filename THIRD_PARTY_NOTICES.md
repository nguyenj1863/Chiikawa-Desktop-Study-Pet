# Licensing Scope and Third-Party Notices

## Original Source Code (MIT License)

The MIT License in `LICENSE` applies **only** to original software source code, build scripts, tests, and synthesized audio assets (`assets/sounds/*.wav`) authored for this repository by John Nguyen.

The MIT License does **not** cover third-party artwork, characters, libraries, or runtime binaries listed below.

---

## Third-Party Artwork and Character Assets

- `assets/chiikawa_fire.gif`
- `assets/chiikawa_skip.gif`
- `assets/chiikawa_stomp.gif`
- `assets/chiikawa_twirl.gif`
- `assets/chiikawa.ico` (derived from the above animation frame)

Chiikawa characters and associated animations are third-party intellectual property and remain subject to their respective rights holders' terms. They are **not** licensed under the MIT License, and excluding them from the MIT grant does not grant permission to redistribute or reuse them commercially.

---

## Qt 6 Framework (LGPLv3)

This application uses the following Qt 6 open-source modules dynamically linked as shared libraries (`Qt6*.dll`):

- `Qt6Core`
- `Qt6Gui`
- `Qt6Widgets`
- `Qt6Multimedia`
- `Qt6Network` (runtime dependency)
- `Qt6Svg` (icon plugin dependency)
- `Qt6Test` (automated tests only, not redistributed)

These Qt 6 modules are licensed under the **GNU Lesser General Public License v3.0 (LGPLv3)** (or other applicable Qt license). Copyright (C) The Qt Company Ltd. and other contributors.

### LGPLv3 Compliance Notes for Binary Distribution

- Qt libraries are **dynamically linked** (`Qt6*.dll`) so end users can replace them with compatible modified versions.
- When redistributing compiled Windows binaries containing Qt DLLs, include this notice, a copy of the LGPLv3 license text (available at <https://www.gnu.org/licenses/lgpl-3.0.html>), and information on obtaining the corresponding Qt source code from <https://download.qt.io/official_releases/qt/>.

---

## FFmpeg (via Qt Multimedia)

When packaged with `windeployqt`, Qt Multimedia's FFmpeg plugin may deploy dynamically linked FFmpeg libraries (`avcodec-61.dll`, `avformat-61.dll`, `avutil-59.dll`, `swresample-5.dll`, `swscale-8.dll`).

- Qt's official prebuilt Windows Qt 6 Multimedia binaries ship FFmpeg built under the **GNU Lesser General Public License v2.1 or later (LGPLv2.1+)**.
- FFmpeg remains subject to its own license terms (<https://ffmpeg.org/legal.html>) and is dynamically linked so the DLLs remain replaceable.

---

## Bundled Open-Source Fonts (SIL Open Font License 1.1)

The onboarding interface bundles two open-source typefaces in `assets/fonts/` under the **SIL Open Font License, Version 1.1** (<https://openfontlicense.org/>):

- **Pixelify Sans** (`assets/fonts/PixelifySans.ttf`) — Copyright 2023 The Pixelify Sans Project Authors (<https://github.com/eifetx/Pixelify-Sans>). Licensed under SIL OFL 1.1.
- **Nunito** (`assets/fonts/Nunito.ttf`) — Copyright 2014 The Nunito Project Authors (<https://github.com/googlefonts/nunito>). Licensed under SIL OFL 1.1.
