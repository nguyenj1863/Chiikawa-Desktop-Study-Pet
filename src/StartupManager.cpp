#include "StartupManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
namespace {
QString runKeyPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
}
#endif

bool StartupManager::isEnabled()
{
#ifdef Q_OS_WIN
    QSettings settings(runKeyPath(), QSettings::NativeFormat);
    return settings.contains(QLatin1String(kRunValueName));
#else
    return false;
#endif
}

bool StartupManager::isStableExecutablePath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath()).toLower();
    if (native.contains(QStringLiteral("\\temp\\"))
        || native.contains(QStringLiteral("\\tmp\\"))
        || native.contains(QStringLiteral("\\appdata\\local\\temp\\"))) {
        return false;
    }
    // Warn-style soft check: build trees are OK for testing but noted as unstable
    if (native.contains(QStringLiteral("\\build\\"))
        || native.contains(QStringLiteral("\\build-"))
        || native.contains(QStringLiteral("\\out\\"))) {
        return false;
    }
    return true;
}

bool StartupManager::enable(const QString& executablePath)
{
#ifdef Q_OS_WIN
    const QFileInfo info(executablePath);
    if (!info.exists()) {
        return false;
    }
    const QString absolute = QDir::toNativeSeparators(info.absoluteFilePath());
    // Quote path so spaces work: "C:\Path With Spaces\app.exe"
    const QString value = QStringLiteral("\"%1\"").arg(absolute);

    QSettings settings(runKeyPath(), QSettings::NativeFormat);
    settings.setValue(QLatin1String(kRunValueName), value);
    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    Q_UNUSED(executablePath);
    return false;
#endif
}

bool StartupManager::disable()
{
#ifdef Q_OS_WIN
    QSettings settings(runKeyPath(), QSettings::NativeFormat);
    settings.remove(QLatin1String(kRunValueName));
    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    return true;
#endif
}
