#include "quickactions/InstalledAppDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <winver.h>
#  include <vector>
#endif

namespace {

QMutex g_cacheMutex;
bool g_cacheValid = false;
QVector<InstalledAppDiscovery::DetectedApp> g_cachedApps;
InstalledAppDiscovery::ProviderFn g_testProvider;

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed())).toLower();
}

bool isShortcutFile(const QString& path)
{
    return path.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive);
}

#ifdef Q_OS_WIN

// Reads the FileDescription (or ProductName) version resource so App Paths
// entries such as "chrome.exe" are listed under their product name.
QString readExeDescription(const QString& exePath)
{
    const std::wstring wPath = QDir::toNativeSeparators(exePath).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(wPath.c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(wPath.c_str(), 0, size, data.data())) {
        return {};
    }

    struct LangCodePage {
        WORD language;
        WORD codePage;
    };
    LangCodePage* translations = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<LPVOID*>(&translations), &translationBytes)
        || translationBytes < sizeof(LangCodePage)) {
        return {};
    }

    for (const wchar_t* field : {L"FileDescription", L"ProductName"}) {
        wchar_t query[128]{};
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s",
                   translations[0].language, translations[0].codePage, field);
        wchar_t* value = nullptr;
        UINT valueLen = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<LPVOID*>(&value), &valueLen)
            && value && valueLen > 1) {
            const QString text = QString::fromWCharArray(value).trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

void scanStartMenuDir(const QString& rootPath, QVector<InstalledAppDiscovery::DetectedApp>& out)
{
    const QDir root(rootPath);
    if (rootPath.isEmpty() || !root.exists()) {
        return;
    }
    QDirIterator it(rootPath, {QStringLiteral("*.lnk")},
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        const QString name = info.completeBaseName().trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const QString folder = root.relativeFilePath(info.absolutePath());
        const QString location = (folder.isEmpty() || folder == QStringLiteral("."))
            ? QStringLiteral("Start Menu")
            : folder;
        out.push_back({name, info.absoluteFilePath(), location, true, {}});
    }
}

// App Paths also registers helper processes that apps launch internally
// (message hosts, pairing helpers, servers); those are not worth suggesting.
bool looksLikeBackgroundComponent(const QString& name, const QString& exeBaseName)
{
    static const QRegularExpression kComponentWords(QStringLiteral(
        "(helper|host|server|service|component|handler|shim|broker|agent|background|"
        "pairing|telemetry|daemon)"),
        QRegularExpression::CaseInsensitiveOption);
    return kComponentWords.match(name).hasMatch() || kComponentWords.match(exeBaseName).hasMatch();
}

void scanAppPathsHive(HKEY root, QVector<InstalledAppDiscovery::DetectedApp>& out)
{
    HKEY parentKey = nullptr;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths",
                      0, KEY_READ, &parentKey) != ERROR_SUCCESS || !parentKey) {
        return;
    }

    DWORD index = 0;
    wchar_t subName[256]{};
    DWORD subNameLen = 256;
    while (RegEnumKeyExW(parentKey, index++, subName, &subNameLen,
                         nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY appKey = nullptr;
        if (RegOpenKeyExW(parentKey, subName, 0, KEY_READ, &appKey) == ERROR_SUCCESS && appKey) {
            wchar_t pathBuf[MAX_PATH * 2]{};
            DWORD bytes = sizeof(pathBuf);
            DWORD type = 0;
            if (RegQueryValueExW(appKey, nullptr, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(pathBuf), &bytes) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ)
                && bytes >= sizeof(wchar_t)) {
                QString exePath = QString::fromWCharArray(pathBuf).trimmed();
                if (exePath.startsWith(QLatin1Char('"')) && exePath.endsWith(QLatin1Char('"')) && exePath.size() >= 2) {
                    exePath = exePath.mid(1, exePath.size() - 2);
                }
                const QFileInfo info(exePath);
                if (info.exists() && info.isFile() && info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0) {
                    QString name = readExeDescription(info.absoluteFilePath());
                    if (name.isEmpty()) {
                        name = info.completeBaseName().trimmed();
                    }
                    if (!name.isEmpty()) {
                        const bool suggested = !looksLikeBackgroundComponent(name, info.completeBaseName());
                        out.push_back({name, info.absoluteFilePath(), QStringLiteral("Registered App"),
                                       suggested, info.absoluteFilePath()});
                    }
                }
            }
            RegCloseKey(appKey);
        }
        subNameLen = 256;
    }
    RegCloseKey(parentKey);
}

#elif defined(Q_OS_MACOS)

void scanMacAppsDir(const QString& dirPath,
                    const QString& subtitle,
                    QVector<InstalledAppDiscovery::DetectedApp>& out)
{
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0) {
            const QString name = entry.completeBaseName().trimmed();
            if (!name.isEmpty()) {
                out.push_back({name, entry.absoluteFilePath(), subtitle, true, entry.absoluteFilePath()});
            }
        } else {
            // Check 1 level deeper (e.g. /Applications/Utilities)
            const QDir sub(entry.absoluteFilePath());
            for (const QFileInfo& nested : sub.entryInfoList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString name = nested.completeBaseName().trimmed();
                if (!name.isEmpty()) {
                    out.push_back({name, nested.absoluteFilePath(), entry.fileName(), true, nested.absoluteFilePath()});
                }
            }
        }
    }
}

#endif

int matchRank(const QString& lowerName, const QString& needle)
{
    if (lowerName.startsWith(needle)) {
        return 0;
    }
    const int idx = lowerName.indexOf(needle);
    if (idx > 0 && !lowerName.at(idx - 1).isLetterOrNumber()) {
        return 1;
    }
    return idx >= 0 ? 2 : 3;
}

} // namespace

bool InstalledAppDiscovery::isLikelyUserFacing(const DetectedApp& app)
{
    const QString name = app.displayName.toLower();
    const QString location = app.subtitle.toLower();
    const QString target = normalizedPath(app.targetPath.isEmpty() ? app.launchReference : app.targetPath);

    // Maintenance, documentation, and developer-plumbing entries.
    static const QRegularExpression kMaintenanceWords(QStringLiteral(
        "\\b(uninstall|uninstaller|uninst|updater|update|updates|help|readme|manual|documentation|"
        "website|homepage|license|eula|changelog|support|feedback|diagnostic|diagnostics|"
        "troubleshooter|troubleshooting|repair|recovery|debug|debugger|crash|reporter|setup|"
        "installer|configuration|config|redistributable|runtime|sdk|driver|drivers|telemetry)\\b"));
    static const QStringList kSystemPhrases = {
        QStringLiteral("read me"), QStringLiteral("release notes"), QStringLiteral("what's new"),
        QStringLiteral("safe mode"), QStringLiteral("command prompt"), QStringLiteral("administrative tools"),
        QStringLiteral("system information"), QStringLiteral("system configuration"),
        QStringLiteral("event viewer"), QStringLiteral("task scheduler"), QStringLiteral("disk cleanup"),
        QStringLiteral("defragment"), QStringLiteral("resource monitor"), QStringLiteral("performance monitor"),
        QStringLiteral("registry editor"), QStringLiteral("component services"),
        QStringLiteral("computer management"), QStringLiteral("odbc"), QStringLiteral("iscsi"),
        QStringLiteral("print management"), QStringLiteral("recovery drive"),
        QStringLiteral("on-screen keyboard"), QStringLiteral("character map"), QStringLiteral("steps recorder"),
        QStringLiteral("windows fax"), QStringLiteral("xps viewer"), QStringLiteral("local security policy")
    };
    if (name == QStringLiteral("services") || kMaintenanceWords.match(name).hasMatch()) {
        return false;
    }
    for (const QString& phrase : kSystemPhrases) {
        if (name.contains(phrase)) {
            return false;
        }
    }

    // Start Menu folders and bundle locations that group operating-system utilities.
    static const QStringList kSystemLocations = {
        QStringLiteral("administrative tools"), QStringLiteral("windows tools"),
        QStringLiteral("system tools"), QStringLiteral("windows system"),
        QStringLiteral("accessibility"), QStringLiteral("windows powershell"),
        QStringLiteral("maintenance"), QStringLiteral("startup"), QStringLiteral("windows kits"),
        QStringLiteral("office tools"), QStringLiteral("utilities")
    };
    for (const QString& loc : kSystemLocations) {
        if (location.contains(loc)) {
            return false;
        }
    }

    // Shortcuts to documents, web links, or management consoles rather than apps.
    static const QStringList kNonAppSuffixes = {
        QStringLiteral(".url"), QStringLiteral(".chm"), QStringLiteral(".txt"), QStringLiteral(".pdf"),
        QStringLiteral(".htm"), QStringLiteral(".html"), QStringLiteral(".rtf"), QStringLiteral(".msc"),
        QStringLiteral(".cpl"), QStringLiteral(".bat"), QStringLiteral(".cmd"), QStringLiteral(".ps1"),
        QStringLiteral(".vbs"), QStringLiteral(".ini"), QStringLiteral(".log"), QStringLiteral(".md"),
        QStringLiteral(".xml")
    };
    for (const QString& suffix : kNonAppSuffixes) {
        if (target.endsWith(suffix)) {
            return false;
        }
    }

    // Built-in operating-system binaries and bundles.
    QString systemRoot = normalizedPath(qEnvironmentVariable("SystemRoot"));
    if (systemRoot.isEmpty()) {
        systemRoot = QStringLiteral("c:/windows");
    }
    if (target.startsWith(systemRoot + QLatin1Char('/'))
        || target.contains(QStringLiteral("/applications/utilities/"))
        || target.startsWith(QStringLiteral("/system/library/"))) {
        return false;
    }
    return true;
}

QVector<InstalledAppDiscovery::DetectedApp> InstalledAppDiscovery::finalize(QVector<DetectedApp> apps)
{
    QVector<DetectedApp> unique;
    unique.reserve(apps.size());
    QHash<QString, qsizetype> indexByName;
    QHash<QString, qsizetype> indexByTarget;

    for (DetectedApp& app : apps) {
        app.displayName = app.displayName.trimmed();
        if (app.displayName.isEmpty() || app.launchReference.trimmed().isEmpty()) {
            continue;
        }
        if (app.targetPath.isEmpty()) {
            app.targetPath = isShortcutFile(app.launchReference)
                ? QFileInfo(app.launchReference).symLinkTarget()
                : app.launchReference;
        }
        // Shortcut whose resolved target no longer exists: it cannot be launched.
        if (isShortcutFile(app.launchReference) && !app.targetPath.isEmpty()
            && !QFileInfo::exists(app.targetPath)) {
            continue;
        }
        app.suggested = app.suggested && isLikelyUserFacing(app);

        const QString nameKey = app.displayName.toLower();
        const QString targetKey = normalizedPath(app.targetPath.isEmpty() ? app.launchReference : app.targetPath);
        const qsizetype existing = indexByName.value(nameKey, indexByTarget.value(targetKey, -1));
        if (existing >= 0) {
            // Keep the first entry unless a later duplicate is the user-facing one
            // (e.g. a Startup-folder copy listed before the main shortcut).
            if (app.suggested && !unique[existing].suggested) {
                unique[existing] = std::move(app);
                indexByName.insert(nameKey, existing);
                indexByTarget.insert(targetKey, existing);
            }
            continue;
        }
        indexByName.insert(nameKey, unique.size());
        indexByTarget.insert(targetKey, unique.size());
        unique.push_back(std::move(app));
    }

    std::sort(unique.begin(), unique.end(), [](const DetectedApp& a, const DetectedApp& b) {
        return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
    });
    return unique;
}

QVector<InstalledAppDiscovery::DetectedApp> InstalledAppDiscovery::scanPlatformApps()
{
    ProviderFn provider;
    {
        QMutexLocker lock(&g_cacheMutex);
        provider = g_testProvider;
    }
    if (provider) {
        return finalize(provider());
    }

    QVector<DetectedApp> apps;
#ifdef Q_OS_WIN
    const QString userStartMenu = QDir(qEnvironmentVariable("APPDATA"))
        .filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    const QString commonStartMenu = QDir(qEnvironmentVariable("PROGRAMDATA"))
        .filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    scanStartMenuDir(userStartMenu, apps);
    scanStartMenuDir(commonStartMenu, apps);
    scanAppPathsHive(HKEY_CURRENT_USER, apps);
    scanAppPathsHive(HKEY_LOCAL_MACHINE, apps);
#elif defined(Q_OS_MACOS)
    scanMacAppsDir(QStringLiteral("/Applications"), QStringLiteral("Applications"), apps);
    scanMacAppsDir(QStringLiteral("/System/Applications"), QStringLiteral("System"), apps);
    const QString homeApps = QDir::home().filePath(QStringLiteral("Applications"));
    scanMacAppsDir(homeApps, QStringLiteral("User Applications"), apps);
#endif
    return finalize(std::move(apps));
}

QVector<InstalledAppDiscovery::DetectedApp> InstalledAppDiscovery::discover(bool forceRefresh)
{
    {
        QMutexLocker lock(&g_cacheMutex);
        if (g_cacheValid && !forceRefresh) {
            return g_cachedApps;
        }
    }

    QVector<DetectedApp> fresh = scanPlatformApps();
    {
        QMutexLocker lock(&g_cacheMutex);
        g_cachedApps = fresh;
        g_cacheValid = true;
    }
    return fresh;
}

void InstalledAppDiscovery::discoverAsync(QObject* receiver,
                                          std::function<void(const QVector<DetectedApp>&)> onReady,
                                          bool forceRefresh)
{
    if (!receiver || !onReady) {
        return;
    }

    {
        QMutexLocker lock(&g_cacheMutex);
        if (g_cacheValid && !forceRefresh) {
            const QVector<DetectedApp> cached = g_cachedApps;
            QPointer<QObject> guard(receiver);
            QMetaObject::invokeMethod(receiver, [guard, cb = std::move(onReady), cached]() {
                if (guard) {
                    cb(cached);
                }
            }, Qt::QueuedConnection);
            return;
        }
    }

    QPointer<QObject> guard(receiver);
    QThread* worker = QThread::create([guard, cb = std::move(onReady), forceRefresh]() mutable {
        const QVector<DetectedApp> apps = InstalledAppDiscovery::discover(forceRefresh);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, cb = std::move(cb), apps]() {
            if (guard) {
                cb(apps);
            }
        }, Qt::QueuedConnection);
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void InstalledAppDiscovery::clearCache()
{
    QMutexLocker lock(&g_cacheMutex);
    g_cacheValid = false;
    g_cachedApps.clear();
}

void InstalledAppDiscovery::setTestProvider(ProviderFn provider)
{
    QMutexLocker lock(&g_cacheMutex);
    g_testProvider = std::move(provider);
    g_cacheValid = false;
    g_cachedApps.clear();
}

QVector<InstalledAppDiscovery::DetectedApp> InstalledAppDiscovery::filter(const QVector<DetectedApp>& apps,
                                                                          const QString& query,
                                                                          bool showAll)
{
    const QString needle = query.trimmed().toLower();
    QVector<DetectedApp> matches;
    matches.reserve(apps.size());

    if (needle.isEmpty()) {
        for (const DetectedApp& app : apps) {
            if (showAll || app.suggested) {
                matches.push_back(app);
            }
        }
        return matches;
    }

    for (const DetectedApp& app : apps) {
        if (app.displayName.toLower().contains(needle)
            || app.subtitle.toLower().contains(needle)) {
            matches.push_back(app);
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [&needle](const DetectedApp& a, const DetectedApp& b) {
        if (a.suggested != b.suggested) {
            return a.suggested;
        }
        return matchRank(a.displayName.toLower(), needle) < matchRank(b.displayName.toLower(), needle);
    });
    return matches;
}
