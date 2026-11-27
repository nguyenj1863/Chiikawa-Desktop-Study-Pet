#include "quickactions/QuickActionItem.h"

#include "quickactions/ExternalAppLauncher.h"
#include "ui/PixelTheme.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {

constexpr qint64 kMaxCustomIconFileBytes = 10LL * 1024LL * 1024LL; // 10 MB safety cap
constexpr int kManagedIconDimension = 128;

QString typeToString(QuickActionType type)
{
    switch (type) {
    case QuickActionType::BuiltInApp: return QStringLiteral("builtin_app");
    case QuickActionType::CustomApp:  return QStringLiteral("custom_app");
    case QuickActionType::Settings:   return QStringLiteral("settings");
    case QuickActionType::Exit:       return QStringLiteral("exit");
    case QuickActionType::Study:      return QStringLiteral("study");
    }
    return QStringLiteral("custom_app");
}

bool stringToType(const QString& str, QuickActionType& out)
{
    if (str == QStringLiteral("builtin_app")) { out = QuickActionType::BuiltInApp; return true; }
    if (str == QStringLiteral("custom_app"))  { out = QuickActionType::CustomApp;  return true; }
    if (str == QStringLiteral("settings"))    { out = QuickActionType::Settings;   return true; }
    if (str == QStringLiteral("exit"))        { out = QuickActionType::Exit;       return true; }
    if (str == QStringLiteral("study"))       { out = QuickActionType::Study;      return true; }
    return false;
}

QString sanitizeFileToken(const QString& id)
{
    QString out;
    out.reserve(id.size());
    for (QChar ch : id) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            out.append(ch);
        } else {
            out.append(QLatin1Char('_'));
        }
    }
    return out.isEmpty() ? QStringLiteral("icon") : out;
}

} // namespace

QVector<QuickActionItem> QuickActionItem::defaultShortcuts()
{
    return {
        {QStringLiteral("builtin:spotify"),  QStringLiteral("Spotify"),
         QuickActionType::BuiltInApp, QStringLiteral("spotify"), {}, true},
        {QStringLiteral("builtin:discord"),  QStringLiteral("Discord"),
         QuickActionType::BuiltInApp, QStringLiteral("discord"), {}, true},
        {QStringLiteral("builtin:league"),   QStringLiteral("League of Legends"),
         QuickActionType::BuiltInApp, QStringLiteral("league"),  {}, true},
        {QStringLiteral("builtin:vscode"),   QStringLiteral("Visual Studio Code"),
         QuickActionType::BuiltInApp, QStringLiteral("vscode"),  {}, true},
        {QStringLiteral("builtin:study"),    QStringLiteral("Study"),
         QuickActionType::Study,      {},                        {}, true},
        {QStringLiteral("builtin:settings"), QStringLiteral("Settings"),
         QuickActionType::Settings,   {},                        {}, true},
        {QStringLiteral("builtin:exit"),     QStringLiteral("Exit"),
         QuickActionType::Exit,       {},                        {}, true}
    };
}

QString QuickActionItem::generateCustomId()
{
    return QStringLiteral("custom:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonArray QuickActionItem::toJsonArray(const QVector<QuickActionItem>& items)
{
    QJsonArray arr;
    for (const QuickActionItem& item : items) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("name"), item.displayName);
        obj.insert(QStringLiteral("type"), typeToString(item.type));
        obj.insert(QStringLiteral("appRef"), item.applicationReference);
        obj.insert(QStringLiteral("icon"), item.customIconPath);
        obj.insert(QStringLiteral("enabled"), item.enabled);
        arr.append(obj);
    }
    return arr;
}

QVector<QuickActionItem> QuickActionItem::fromJsonArray(const QJsonArray& array, bool* ok)
{
    if (ok) {
        *ok = false;
    }
    if (array.isEmpty()) {
        return {};
    }

    QVector<QuickActionItem> parsed;
    parsed.reserve(array.size());
    for (const QJsonValue& val : array) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        QuickActionType type = QuickActionType::CustomApp;
        if (!stringToType(obj.value(QStringLiteral("type")).toString(), type)) {
            continue;
        }
        QuickActionItem item;
        item.id = obj.value(QStringLiteral("id")).toString().trimmed();
        item.displayName = obj.value(QStringLiteral("name")).toString().trimmed();
        item.type = type;
        item.applicationReference = obj.value(QStringLiteral("appRef")).toString().trimmed();
        item.customIconPath = obj.value(QStringLiteral("icon")).toString().trimmed();
        item.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        parsed.push_back(std::move(item));
    }

    const QVector<QuickActionItem> sanitized = sanitizeList(parsed);
    if (ok) {
        *ok = !sanitized.isEmpty();
    }
    return sanitized;
}

bool QuickActionItem::migrateStoredList(QJsonArray& array, int fromVersion)
{
    if (fromVersion >= kStoredVersion) {
        return false;
    }
    bool changed = false;
    bool hasStudy = false;
    qsizetype settingsIndex = -1;
    int enabledCount = 0;

    for (qsizetype i = 0; i < array.size(); ++i) {
        QJsonObject obj = array.at(i).toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("builtin_app")
            && obj.value(QStringLiteral("appRef")).toString() == QStringLiteral("cursor")) {
            obj.insert(QStringLiteral("id"), QStringLiteral("builtin:vscode"));
            obj.insert(QStringLiteral("appRef"), QStringLiteral("vscode"));
            // Only the stock name changes; a name the user chose for the shortcut is kept.
            if (obj.value(QStringLiteral("name")).toString().trimmed() == QStringLiteral("Cursor")) {
                obj.insert(QStringLiteral("name"), QStringLiteral("Visual Studio Code"));
            }
            array.replace(i, obj);
            changed = true;
        }
        if (type == QStringLiteral("study")) {
            hasStudy = true;
        }
        if (type == QStringLiteral("settings") && settingsIndex < 0) {
            settingsIndex = i;
        }
        if (obj.value(QStringLiteral("enabled")).toBool(true)) {
            ++enabledCount;
        }
    }

    if (!hasStudy && array.size() < kMaxConfiguredShortcuts) {
        QJsonObject study;
        study.insert(QStringLiteral("id"), QStringLiteral("builtin:study"));
        study.insert(QStringLiteral("name"), QStringLiteral("Study"));
        study.insert(QStringLiteral("type"), QStringLiteral("study"));
        study.insert(QStringLiteral("appRef"), QString());
        study.insert(QStringLiteral("icon"), QString());
        // The radial menu shows at most 10 shortcuts; never push another one out silently.
        study.insert(QStringLiteral("enabled"), enabledCount < kMaxVisibleRadialShortcuts);
        const qsizetype at = settingsIndex >= 0 ? settingsIndex : array.size();
        array.insert(at, study);
        changed = true;
    }
    return changed;
}

QVector<QuickActionItem> QuickActionItem::sanitizeList(const QVector<QuickActionItem>& items)
{
    QVector<QuickActionItem> result;
    result.reserve(std::min(static_cast<int>(items.size()), kMaxConfiguredShortcuts));
    QSet<QString> seenIds;

    for (QuickActionItem item : items) {
        if (result.size() >= kMaxConfiguredShortcuts) {
            break;
        }
        if (item.id.isEmpty()) {
            item.id = generateCustomId();
        }
        if (seenIds.contains(item.id)) {
            continue;
        }

        // Preserve internal action integrity so external paths can never hijack Settings or Exit.
        if (item.type == QuickActionType::Settings) {
            item.applicationReference.clear();
            if (item.displayName.isEmpty()) {
                item.displayName = QStringLiteral("Settings");
            }
        } else if (item.type == QuickActionType::Exit) {
            item.applicationReference.clear();
            if (item.displayName.isEmpty()) {
                item.displayName = QStringLiteral("Exit");
            }
        } else if (item.type == QuickActionType::Study) {
            item.applicationReference.clear();
            if (item.displayName.isEmpty()) {
                item.displayName = QStringLiteral("Study");
            }
        } else if (item.type == QuickActionType::BuiltInApp) {
            if (item.applicationReference != QStringLiteral("spotify")
                && item.applicationReference != QStringLiteral("discord")
                && item.applicationReference != QStringLiteral("league")
                && item.applicationReference != QStringLiteral("vscode")) {
                continue;
            }
            if (item.displayName.isEmpty()) {
                item.displayName = item.applicationReference;
            }
        } else {
            if (item.applicationReference.isEmpty()) {
                continue;
            }
            if (item.displayName.isEmpty()) {
                item.displayName = ExternalAppLauncher::detectAppDisplayName(item.applicationReference);
            }
        }

        seenIds.insert(item.id);
        result.push_back(std::move(item));
    }
    return result;
}

QString QuickActionItem::importCustomIcon(const QString& sourceImagePath,
                                          const QString& shortcutId,
                                          QString* errorOut)
{
    const QFileInfo info(sourceImagePath);
    if (!info.exists() || !info.isFile()) {
        if (errorOut) *errorOut = QStringLiteral("The selected icon file could not be found.");
        return {};
    }
    if (info.size() <= 0 || info.size() > kMaxCustomIconFileBytes) {
        if (errorOut) *errorOut = QStringLiteral("The selected icon file is empty or too large.");
        return {};
    }

    QImageReader reader(sourceImagePath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        if (errorOut) *errorOut = QStringLiteral("Unsupported or corrupted image file.");
        return {};
    }

    QImage scaled = (img.width() > kManagedIconDimension || img.height() > kManagedIconDimension)
        ? img.scaled(kManagedIconDimension, kManagedIconDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : img;

    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QDir iconDir(QDir(baseDir).filePath(QStringLiteral("quick_action_icons")));
    if (!iconDir.exists() && !iconDir.mkpath(QStringLiteral("."))) {
        if (errorOut) *errorOut = QStringLiteral("Could not create icon storage directory.");
        return {};
    }

    const QString destPath = iconDir.filePath(sanitizeFileToken(shortcutId) + QStringLiteral(".png"));
    if (!scaled.save(destPath, "PNG")) {
        if (errorOut) *errorOut = QStringLiteral("Could not save the imported icon.");
        return {};
    }
    return destPath;
}

QIcon QuickActionItem::resolveIcon(const QuickActionItem& item, int logicalSize)
{
    // 1. User-imported custom icon if present and readable
    if (!item.customIconPath.isEmpty() && QFileInfo::exists(item.customIconPath)) {
        QImageReader reader(item.customIconPath);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (!img.isNull()) {
            const int target = std::max(16, logicalSize * 3);
            const QImage scaled = img.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap pm = QPixmap::fromImage(scaled);
            pm.setDevicePixelRatio(3.0);
            return QIcon(pm);
        }
    }

    // 2. Built-in icons via PixelTheme
    if (item.type == QuickActionType::BuiltInApp) {
        if (item.applicationReference == QStringLiteral("spotify")) {
            return PixelTheme::icon(PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54), logicalSize);
        }
        if (item.applicationReference == QStringLiteral("discord")) {
            return PixelTheme::icon(PixelIcon::Discord, QColor(0x58, 0x65, 0xF2), logicalSize);
        }
        if (item.applicationReference == QStringLiteral("league")) {
            return PixelTheme::icon(PixelIcon::LeagueOfLegends, QColor(0xC8, 0xAA, 0x6E), logicalSize);
        }
        if (item.applicationReference == QStringLiteral("vscode")) {
            // Prefer the installed application's own icon; fall back to a generic code mark.
            const QString exe = ExternalAppLauncher::installedAppPath(ExternalAppLauncher::AppId::VSCode);
            if (!exe.isEmpty()) {
                const QIcon native = ExternalAppLauncher::extractAppIcon(exe);
                if (!native.isNull()) {
                    return native;
                }
            }
            return PixelTheme::icon(PixelIcon::Code, QColor(0x4D, 0xA3, 0xFF), logicalSize);
        }
    }
    if (item.type == QuickActionType::Study) {
        return PixelTheme::icon(PixelIcon::Study, PixelTheme::kPrimaryAccent, logicalSize);
    }
    if (item.type == QuickActionType::Settings) {
        return PixelTheme::icon(PixelIcon::Gear, PixelTheme::kPrimaryText, logicalSize);
    }
    if (item.type == QuickActionType::Exit) {
        return PixelTheme::icon(PixelIcon::Exit, PixelTheme::kPrimaryText, logicalSize);
    }

    // 3. Native detected application icon for custom desktop apps
    if (!item.applicationReference.isEmpty()) {
        const QIcon nativeIcon = ExternalAppLauncher::extractAppIcon(item.applicationReference);
        if (!nativeIcon.isNull()) {
            return nativeIcon;
        }
    }

    // 4. Generic fallback icon
    return PixelTheme::icon(PixelIcon::GenericApp, PixelTheme::kPrimaryText, logicalSize);
}
