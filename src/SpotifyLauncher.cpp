#include "SpotifyLauncher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#  include <cwctype>
#  include <string>
#endif

#ifdef Q_OS_MACOS
namespace SpotifyLauncherMac {
bool activateRunning();
bool isInstalled();
bool launchInstalled();
} // namespace SpotifyLauncherMac
#endif

namespace {

#ifdef Q_OS_WIN

bool equalsIgnoreCase(const std::wstring& a, const wchar_t* b)
{
    const std::wstring rhs(b);
    if (a.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(rhs[i])) {
            return false;
        }
    }
    return true;
}

bool isSpotifyProcess(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    wchar_t buffer[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer, &size);
    CloseHandle(process);
    if (!ok || size == 0) {
        return false;
    }

    std::wstring path(buffer, size);
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring fileName = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return equalsIgnoreCase(fileName, L"spotify.exe");
}

struct FindWindowContext {
    HWND bestHwnd = nullptr;
};

BOOL CALLBACK enumSpotifyWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<FindWindowContext*>(lParam);
    if (!ctx) {
        return FALSE;
    }

    // Skip owned helper/tool windows and hidden background windows.
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || !isSpotifyProcess(pid)) {
        return TRUE;
    }

    // Prefer top-level Spotify windows that have a non-empty caption (main player window).
    if (GetWindowTextLengthW(hwnd) > 0) {
        ctx->bestHwnd = hwnd;
        return FALSE;
    }
    if (!ctx->bestHwnd) {
        ctx->bestHwnd = hwnd;
    }
    return TRUE;
}

bool winActivateRunningSpotify()
{
    FindWindowContext ctx;
    EnumWindows(enumSpotifyWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.bestHwnd) {
        return false;
    }

    if (IsIconic(ctx.bestHwnd)) {
        ShowWindow(ctx.bestHwnd, SW_RESTORE);
    } else {
        ShowWindow(ctx.bestHwnd, SW_SHOW);
    }
    BringWindowToTop(ctx.bestHwnd);
    SetForegroundWindow(ctx.bestHwnd);
    return true;
}

bool winRegistryKeyExists(HKEY root, const wchar_t* subKey)
{
    HKEY key = nullptr;
    const LONG res = RegOpenKeyExW(root, subKey, 0, KEY_READ, &key);
    if (res == ERROR_SUCCESS && key) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

bool winHasSpotifyUriScheme()
{
    return winRegistryKeyExists(HKEY_CURRENT_USER, L"Software\\Classes\\spotify")
        || winRegistryKeyExists(HKEY_CLASSES_ROOT, L"spotify");
}

QString winFindSpotifyExecutable()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString standalone = QDir(appData).filePath(QStringLiteral("Spotify/Spotify.exe"));
        if (QFileInfo::exists(standalone)) {
            return standalone;
        }
    }

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString storeAlias = QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps/Spotify.exe"));
        if (QFileInfo::exists(storeAlias)) {
            return storeAlias;
        }
    }

    return {};
}

bool winIsSpotifyInstalled()
{
    return winHasSpotifyUriScheme() || !winFindSpotifyExecutable().isEmpty();
}

bool winLaunchSpotify()
{
    // Prefer the registered spotify: URI protocol handler because it works uniformly for
    // both Standalone and Microsoft Store (MSIX) installations and activates an existing
    // tray-minimized Spotify instance without spawning duplicates.
    if (winHasSpotifyUriScheme()) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = L"spotify:";
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            return true;
        }
    }

    const QString exePath = winFindSpotifyExecutable();
    if (!exePath.isEmpty()) {
        return QProcess::startDetached(exePath, {});
    }

    return false;
}

#endif // Q_OS_WIN

} // namespace

SpotifyLauncher::SpotifyLauncher()
    : m_backend(defaultPlatformBackend())
{
}

SpotifyLauncher::SpotifyLauncher(Backend backend)
    : m_backend(std::move(backend))
{
}

SpotifyLauncher::Backend SpotifyLauncher::defaultPlatformBackend()
{
#ifdef Q_OS_WIN
    return Backend{
        winActivateRunningSpotify,
        winIsSpotifyInstalled,
        winLaunchSpotify
    };
#elif defined(Q_OS_MACOS)
    return Backend{
        SpotifyLauncherMac::activateRunning,
        SpotifyLauncherMac::isInstalled,
        SpotifyLauncherMac::launchInstalled
    };
#else
    return Backend{
        []() { return false; },
        []() { return !QStandardPaths::findExecutable(QStringLiteral("spotify")).isEmpty(); },
        []() {
            const QString exe = QStandardPaths::findExecutable(QStringLiteral("spotify"));
            return !exe.isEmpty() && QProcess::startDetached(exe, {});
        }
    };
#endif
}

SpotifyLauncher::Result SpotifyLauncher::openOrActivate() const
{
    if (m_backend.activateRunning && m_backend.activateRunning()) {
        return {Status::ActivatedRunning, QStringLiteral("Spotify activated.")};
    }

    const bool installed = m_backend.isInstalled ? m_backend.isInstalled() : false;
    if (!installed) {
        return {Status::NotInstalled,
                QStringLiteral("Spotify isn't installed on this computer.")};
    }

    if (m_backend.launchInstalled && m_backend.launchInstalled()) {
        return {Status::LaunchedApp, QStringLiteral("Spotify launched.")};
    }

    return {Status::LaunchFailed,
            QStringLiteral("Couldn't open Spotify right now.")};
}
