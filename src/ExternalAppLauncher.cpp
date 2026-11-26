#include "ExternalAppLauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#  include <cwctype>
#  include <string>
#  include <vector>
#endif

#ifdef Q_OS_MACOS
namespace ExternalAppLauncherMac {
bool activateRunning(ExternalAppLauncher::AppId app);
bool isInstalled(ExternalAppLauncher::AppId app);
bool launchInstalled(ExternalAppLauncher::AppId app);
} // namespace ExternalAppLauncherMac
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

bool processMatchesAny(DWORD pid, const std::vector<const wchar_t*>& exeNames)
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

    const std::wstring path(buffer, size);
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring fileName = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    for (const wchar_t* candidate : exeNames) {
        if (equalsIgnoreCase(fileName, candidate)) {
            return true;
        }
    }
    return false;
}

struct EnumAppContext {
    std::vector<const wchar_t*> exeNames;
    HWND bestHwnd = nullptr;
};

BOOL CALLBACK enumAppWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumAppContext*>(lParam);
    if (!ctx) {
        return FALSE;
    }
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
    if (pid == 0 || !processMatchesAny(pid, ctx->exeNames)) {
        return TRUE;
    }

    if (GetWindowTextLengthW(hwnd) > 0) {
        ctx->bestHwnd = hwnd;
        return FALSE;
    }
    if (!ctx->bestHwnd) {
        ctx->bestHwnd = hwnd;
    }
    return TRUE;
}

bool winActivateByProcessNames(std::vector<const wchar_t*> exeNames)
{
    EnumAppContext ctx{std::move(exeNames), nullptr};
    EnumWindows(enumAppWindowsProc, reinterpret_cast<LPARAM>(&ctx));
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

QString winReadRegistryDefaultString(HKEY root, const wchar_t* subKey)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS || !key) {
        return {};
    }
    wchar_t buf[MAX_PATH * 2]{};
    DWORD bytes = sizeof(buf);
    DWORD type = 0;
    const LONG res = RegQueryValueExW(key, nullptr, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(buf), &bytes);
    RegCloseKey(key);
    if (res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        QString val = QString::fromWCharArray(buf).trimmed();
        if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')) && val.size() >= 2) {
            val = val.mid(1, val.size() - 2);
        }
        return val;
    }
    return {};
}

bool winShellOpen(const QString& target)
{
    const std::wstring wTarget = target.toStdWString();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = wTarget.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) == TRUE;
}

bool winHasUriScheme(const wchar_t* scheme)
{
    const std::wstring userKey = std::wstring(L"Software\\Classes\\") + scheme;
    return winRegistryKeyExists(HKEY_CURRENT_USER, userKey.c_str())
        || winRegistryKeyExists(HKEY_CLASSES_ROOT, scheme);
}

// --- Spotify (Windows) ---
QString winFindSpotifyExe()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString p = QDir(appData).filePath(QStringLiteral("Spotify/Spotify.exe"));
        if (QFileInfo::exists(p)) return p;
    }
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString p = QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps/Spotify.exe"));
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

// --- Discord (Windows) ---
QString winFindDiscordUpdateExe()
{
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString upd = QDir(localAppData).filePath(QStringLiteral("Discord/Update.exe"));
        if (QFileInfo::exists(upd)) return upd;
    }
    return {};
}

QString winFindDiscordAppExe()
{
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) return {};
    const QDir discordDir(QDir(localAppData).filePath(QStringLiteral("Discord")));
    if (!discordDir.exists()) return {};
    const QStringList appDirs = discordDir.entryList(
        {QStringLiteral("app-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QString& d : appDirs) {
        const QString exe = discordDir.filePath(d + QStringLiteral("/Discord.exe"));
        if (QFileInfo::exists(exe)) return exe;
    }
    return {};
}

QString winFindDiscordShortcut()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString lnk = QDir(appData).filePath(
            QStringLiteral("Microsoft/Windows/Start Menu/Programs/Discord Inc/Discord.lnk"));
        if (QFileInfo::exists(lnk)) return lnk;
    }
    return {};
}

// --- League of Legends (Windows) ---
QString winFindRiotClientServicesExe()
{
    const QString programData = qEnvironmentVariable("PROGRAMDATA");
    if (!programData.isEmpty()) {
        const QString jsonPath = QDir(programData).filePath(
            QStringLiteral("Riot Games/RiotClientInstalls.json"));
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            for (const char* key : {"rc_default", "rc_live", "rc_beta"}) {
                const QString path = obj.value(QLatin1String(key)).toString();
                if (!path.isEmpty() && QFileInfo::exists(path)) {
                    return path;
                }
            }
        }
    }
    return {};
}

QString winFindLeagueShortcut()
{
    const QString programData = qEnvironmentVariable("PROGRAMDATA");
    if (!programData.isEmpty()) {
        const QString lnk = QDir(programData).filePath(
            QStringLiteral("Microsoft/Windows/Start Menu/Programs/Riot Games/League of Legends.lnk"));
        if (QFileInfo::exists(lnk)) return lnk;
    }
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString lnk = QDir(appData).filePath(
            QStringLiteral("Microsoft/Windows/Start Menu/Programs/Riot Games/League of Legends.lnk"));
        if (QFileInfo::exists(lnk)) return lnk;
    }
    return {};
}

// --- Cursor (Windows) ---
QString winFindCursorExe()
{
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        const QString regPath = winReadRegistryDefaultString(
            root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Cursor.exe");
        if (!regPath.isEmpty() && QFileInfo::exists(regPath)) {
            return regPath;
        }
    }
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString p = QDir(localAppData).filePath(QStringLiteral("Programs/cursor/Cursor.exe"));
        if (QFileInfo::exists(p)) return p;
    }
    const QString progFiles = qEnvironmentVariable("ProgramFiles");
    if (!progFiles.isEmpty()) {
        const QString p = QDir(progFiles).filePath(QStringLiteral("cursor/Cursor.exe"));
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

QString winFindCursorShortcut()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString lnk = QDir(appData).filePath(
            QStringLiteral("Microsoft/Windows/Start Menu/Programs/Cursor/Cursor.lnk"));
        if (QFileInfo::exists(lnk)) return lnk;
    }
    return {};
}

#endif // Q_OS_WIN

} // namespace

ExternalAppLauncher::ExternalAppLauncher()
{
    for (int i = 0; i < static_cast<int>(AppId::Count); ++i) {
        const auto id = static_cast<AppId>(i);
        m_backends[static_cast<std::size_t>(i)] = defaultPlatformBackend(id);
    }
}

void ExternalAppLauncher::setBackend(AppId app, Backend backend)
{
    const int idx = static_cast<int>(app);
    if (idx >= 0 && idx < static_cast<int>(AppId::Count)) {
        m_backends[static_cast<std::size_t>(idx)] = std::move(backend);
    }
}

ExternalAppLauncher::AppMetadata ExternalAppLauncher::metadata(AppId app)
{
    switch (app) {
    case AppId::Spotify:
        return {app, QStringLiteral("Spotify"),
                QStringLiteral("Open Web Player"),
                QUrl(QStringLiteral("https://open.spotify.com"))};
    case AppId::Discord:
        return {app, QStringLiteral("Discord"),
                QStringLiteral("Open Discord Web"),
                QUrl(QStringLiteral("https://discord.com/app"))};
    case AppId::LeagueOfLegends:
        return {app, QStringLiteral("League of Legends"),
                QStringLiteral("Visit Website"),
                QUrl(QStringLiteral("https://www.leagueoflegends.com"))};
    case AppId::Cursor:
        return {app, QStringLiteral("Cursor"),
                QStringLiteral("Visit Website"),
                QUrl(QStringLiteral("https://cursor.com"))};
    case AppId::Count:
        break;
    }
    return {app, QStringLiteral("Application"), QStringLiteral("Open Website"), {}};
}

ExternalAppLauncher::Backend ExternalAppLauncher::defaultPlatformBackend(AppId app)
{
#ifdef Q_OS_WIN
    switch (app) {
    case AppId::Spotify:
        return {
            []() { return winActivateByProcessNames({L"spotify.exe"}); },
            []() { return winHasUriScheme(L"spotify") || !winFindSpotifyExe().isEmpty(); },
            []() {
                if (winHasUriScheme(L"spotify") && winShellOpen(QStringLiteral("spotify:"))) {
                    return true;
                }
                const QString exe = winFindSpotifyExe();
                return !exe.isEmpty() && QProcess::startDetached(exe, {});
            }
        };
    case AppId::Discord:
        return {
            []() { return winActivateByProcessNames({L"discord.exe"}); },
            []() {
                return !winFindDiscordUpdateExe().isEmpty()
                    || !winFindDiscordAppExe().isEmpty()
                    || !winFindDiscordShortcut().isEmpty()
                    || winHasUriScheme(L"discord");
            },
            []() {
                const QString upd = winFindDiscordUpdateExe();
                if (!upd.isEmpty() && QProcess::startDetached(upd, {QStringLiteral("--processStart"), QStringLiteral("Discord.exe")})) {
                    return true;
                }
                const QString appExe = winFindDiscordAppExe();
                if (!appExe.isEmpty() && QProcess::startDetached(appExe, {})) {
                    return true;
                }
                const QString lnk = winFindDiscordShortcut();
                if (!lnk.isEmpty() && winShellOpen(lnk)) {
                    return true;
                }
                return winHasUriScheme(L"discord") && winShellOpen(QStringLiteral("discord:"));
            }
        };
    case AppId::LeagueOfLegends:
        return {
            []() {
                return winActivateByProcessNames({
                    L"LeagueClientUx.exe", L"LeagueClient.exe", L"RiotClientUx.exe"
                });
            },
            []() {
                return !winFindLeagueShortcut().isEmpty()
                    || !winFindRiotClientServicesExe().isEmpty();
            },
            []() {
                const QString lnk = winFindLeagueShortcut();
                if (!lnk.isEmpty() && winShellOpen(lnk)) {
                    return true;
                }
                const QString riot = winFindRiotClientServicesExe();
                if (!riot.isEmpty()) {
                    return QProcess::startDetached(riot, {
                        QStringLiteral("--launch-product=league_of_legends"),
                        QStringLiteral("--launch-patchline=live")
                    });
                }
                return false;
            }
        };
    case AppId::Cursor:
        return {
            []() { return winActivateByProcessNames({L"cursor.exe"}); },
            []() {
                return !winFindCursorExe().isEmpty() || !winFindCursorShortcut().isEmpty();
            },
            []() {
                const QString exe = winFindCursorExe();
                if (!exe.isEmpty() && QProcess::startDetached(exe, {})) {
                    return true;
                }
                const QString lnk = winFindCursorShortcut();
                return !lnk.isEmpty() && winShellOpen(lnk);
            }
        };
    case AppId::Count:
        break;
    }
#elif defined(Q_OS_MACOS)
    return {
        [app]() { return ExternalAppLauncherMac::activateRunning(app); },
        [app]() { return ExternalAppLauncherMac::isInstalled(app); },
        [app]() { return ExternalAppLauncherMac::launchInstalled(app); }
    };
#else
    Q_UNUSED(app);
#endif
    return {
        []() { return false; },
        []() { return false; },
        []() { return false; }
    };
}

ExternalAppLauncher::Result ExternalAppLauncher::openOrActivate(AppId app) const
{
    const AppMetadata meta = metadata(app);
    const int idx = static_cast<int>(app);
    if (idx < 0 || idx >= static_cast<int>(AppId::Count)) {
        return {app, Status::LaunchFailed,
                QStringLiteral("Couldn't open application"),
                QStringLiteral("Unknown application shortcut."),
                meta.fallbackActionLabel, meta.fallbackUrl};
    }

    const Backend& backend = m_backends[static_cast<std::size_t>(idx)];
    if (backend.activateRunning && backend.activateRunning()) {
        return {app, Status::ActivatedRunning,
                meta.displayName,
                QStringLiteral("%1 activated.").arg(meta.displayName),
                meta.fallbackActionLabel, meta.fallbackUrl};
    }

    const bool installed = backend.isInstalled ? backend.isInstalled() : false;
    if (!installed) {
        return {app, Status::NotInstalled,
                QStringLiteral("%1 isn't installed").arg(meta.displayName),
                QStringLiteral("Install the %1 desktop app or visit its website.").arg(meta.displayName),
                meta.fallbackActionLabel, meta.fallbackUrl};
    }

    if (backend.launchInstalled && backend.launchInstalled()) {
        return {app, Status::LaunchedApp,
                meta.displayName,
                QStringLiteral("%1 launched.").arg(meta.displayName),
                meta.fallbackActionLabel, meta.fallbackUrl};
    }

    return {app, Status::LaunchFailed,
            QStringLiteral("Couldn't open %1").arg(meta.displayName),
            QStringLiteral("%1 couldn't be started right now.").arg(meta.displayName),
            meta.fallbackActionLabel, meta.fallbackUrl};
}
