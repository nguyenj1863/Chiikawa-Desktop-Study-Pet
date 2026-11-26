#include "InstalledAppDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

QMutex g_cacheMutex;
bool g_cacheValid = false;
QVector<InstalledAppDiscovery::DetectedApp> g_cachedApps;
InstalledAppDiscovery::ProviderFn g_testProvider;

bool isNoiseShortcutName(const QString& lowerName)
{
    static const QStringList kNoise = {
        QStringLiteral("uninstall"),
        QStringLiteral("readme"),
        QStringLiteral("help"),
        QStringLiteral("manual"),
        QStringLiteral("documentation"),
        QStringLiteral("website"),
        QStringLiteral("license"),
        QStringLiteral("release notes"),
        QStringLiteral("command prompt"),
        QStringLiteral("safe mode")
    };
    for (const QString& token : kNoise) {
        if (lowerName.contains(token)) {
            return true;
        }
    }
    return false;
}

#ifdef Q_OS_WIN

void scanStartMenuDir(const QString& dirPath,
                      const QString& subtitle,
                      QVector<InstalledAppDiscovery::DetectedApp>& out)
{
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) {
        return;
    }
    QDirIterator it(dirPath, {QStringLiteral("*.lnk")},
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString lnkPath = it.next();
        const QFileInfo info(lnkPath);
        const QString name = info.completeBaseName().trimmed();
        if (name.isEmpty() || isNoiseShortcutName(name.toLower())) {
            continue;
        }
        out.push_back({name, info.absoluteFilePath(), subtitle});
    }
}

void scanAppPathsHive(HKEY root,
                      QVector<InstalledAppDiscovery::DetectedApp>& out)
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
                    const QString name = info.completeBaseName().trimmed();
                    if (!name.isEmpty() && !isNoiseShortcutName(name.toLower())) {
                        out.push_back({name, info.absoluteFilePath(), QStringLiteral("Installed App")});
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
                out.push_back({name, entry.absoluteFilePath(), subtitle});
            }
        } else {
            // Check 1 level deeper (e.g. /Applications/Utilities)
            const QDir sub(entry.absoluteFilePath());
            for (const QFileInfo& nested : sub.entryInfoList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString name = nested.completeBaseName().trimmed();
                if (!name.isEmpty()) {
                    out.push_back({name, nested.absoluteFilePath(), entry.fileName()});
                }
            }
        }
    }
}

#endif

} // namespace

QVector<InstalledAppDiscovery::DetectedApp> InstalledAppDiscovery::deduplicateAndSort(QVector<DetectedApp> apps)
{
    QVector<DetectedApp> unique;
    unique.reserve(apps.size());
    QSet<QString> seenNames;
    QSet<QString> seenPaths;

    for (DetectedApp& app : apps) {
        const QString normName = app.displayName.trimmed().toLower();
        const QString normPath = QDir::cleanPath(app.launchReference.trimmed()).toLower();
        if (normName.isEmpty() || normPath.isEmpty()) {
            continue;
        }
        if (seenNames.contains(normName) || seenPaths.contains(normPath)) {
            continue;
        }
        seenNames.insert(normName);
        seenPaths.insert(normPath);
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
        return deduplicateAndSort(provider());
    }

    QVector<DetectedApp> apps;
#ifdef Q_OS_WIN
    const QString userStartMenu = QDir(qEnvironmentVariable("APPDATA"))
        .filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    const QString commonStartMenu = QDir(qEnvironmentVariable("PROGRAMDATA"))
        .filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    scanStartMenuDir(userStartMenu, QStringLiteral("Start Menu"), apps);
    scanStartMenuDir(commonStartMenu, QStringLiteral("Start Menu"), apps);
    scanAppPathsHive(HKEY_CURRENT_USER, apps);
    scanAppPathsHive(HKEY_LOCAL_MACHINE, apps);
#elif defined(Q_OS_MACOS)
    scanMacAppsDir(QStringLiteral("/Applications"), QStringLiteral("Applications"), apps);
    scanMacAppsDir(QStringLiteral("/System/Applications"), QStringLiteral("System"), apps);
    const QString homeApps = QDir::home().filePath(QStringLiteral("Applications"));
    scanMacAppsDir(homeApps, QStringLiteral("User Applications"), apps);
#endif
    return deduplicateAndSort(std::move(apps));
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
                                                                          const QString& query)
{
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) {
        return apps;
    }
    QVector<DetectedApp> matches;
    matches.reserve(apps.size());
    for (const DetectedApp& app : apps) {
        if (app.displayName.toLower().contains(needle)
            || app.subtitle.toLower().contains(needle)) {
            matches.push_back(app);
        }
    }
    return matches;
}
