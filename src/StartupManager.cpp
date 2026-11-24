#include "StartupManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

#ifdef Q_OS_WIN
namespace {
QString runKeyPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
}
#elif defined(Q_OS_MACOS)
namespace {
QString launchAgentPath()
{
    return QDir::home().filePath(
        QStringLiteral("Library/LaunchAgents/io.github.nguyenj1863.chiikawastudypet.plist"));
}

QString xmlEscape(const QString& input)
{
    QString out = input;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}
}
#endif

bool StartupManager::isEnabled()
{
#ifdef Q_OS_WIN
    QSettings settings(runKeyPath(), QSettings::NativeFormat);
    return settings.contains(QLatin1String(kRunValueName));
#elif defined(Q_OS_MACOS)
    return QFileInfo::exists(launchAgentPath());
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
    const QString normalized = QDir::fromNativeSeparators(info.absoluteFilePath()).toLower();
    if (normalized.contains(QStringLiteral("/temp/"))
        || normalized.contains(QStringLiteral("/tmp/"))
        || normalized.contains(QStringLiteral("/appdata/local/temp/"))
        || normalized.contains(QStringLiteral("/private/var/folders/"))) {
        return false;
    }
    // Warn-style soft check: build trees are OK for testing but noted as unstable
    if (normalized.contains(QStringLiteral("/build/"))
        || normalized.contains(QStringLiteral("/build-"))
        || normalized.contains(QStringLiteral("/out/"))) {
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
#elif defined(Q_OS_MACOS)
    const QFileInfo info(executablePath);
    if (!info.exists()) {
        return false;
    }
    const QString plistPath = launchAgentPath();
    QDir().mkpath(QFileInfo(plistPath).absolutePath());
    QFile file(plistPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n"
        << "    <key>Label</key>\n"
        << "    <string>io.github.nguyenj1863.chiikawastudypet</string>\n"
        << "    <key>ProgramArguments</key>\n"
        << "    <array>\n"
        << "        <string>" << xmlEscape(info.absoluteFilePath()) << "</string>\n"
        << "    </array>\n"
        << "    <key>RunAtLoad</key>\n"
        << "    <true/>\n"
        << "</dict>\n"
        << "</plist>\n";
    return file.flush();
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
#elif defined(Q_OS_MACOS)
    const QString plistPath = launchAgentPath();
    if (!QFileInfo::exists(plistPath)) {
        return true;
    }
    return QFile::remove(plistPath);
#else
    return true;
#endif
}
