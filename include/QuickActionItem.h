#pragma once

#include <QIcon>
#include <QJsonArray>
#include <QString>
#include <QVector>

enum class QuickActionType {
    BuiltInApp = 0, // Spotify, Discord, League of Legends, Cursor
    CustomApp,      // User-selected native desktop application
    Settings,       // Internal Study Pet Settings panel
    Exit            // Internal clean application shutdown
};

/**
 * Data-driven representation of a single quick-action shortcut in Chiikawa's
 * radial menu. Supports built-in apps, user-added desktop applications, and
 * internal actions (Settings, Exit) with stable IDs and optional custom icons.
 */
struct QuickActionItem {
    static constexpr int kMaxVisibleRadialShortcuts = 10;
    static constexpr int kMaxConfiguredShortcuts = 16;

    QString id;
    QString displayName;
    QuickActionType type = QuickActionType::CustomApp;
    QString applicationReference; // Built-in key ("spotify", "discord", "league", "cursor") or native app path
    QString customIconPath;       // Managed local PNG path if user imported a custom icon
    bool enabled = true;

    [[nodiscard]] bool isBuiltIn() const { return type != QuickActionType::CustomApp; }
    [[nodiscard]] bool isInternal() const
    {
        return type == QuickActionType::Settings || type == QuickActionType::Exit;
    }

    [[nodiscard]] bool operator==(const QuickActionItem& other) const = default;

    [[nodiscard]] static QVector<QuickActionItem> defaultShortcuts();
    [[nodiscard]] static QString generateCustomId();

    [[nodiscard]] static QJsonArray toJsonArray(const QVector<QuickActionItem>& items);
    [[nodiscard]] static QVector<QuickActionItem> fromJsonArray(const QJsonArray& array, bool* ok = nullptr);
    [[nodiscard]] static QVector<QuickActionItem> sanitizeList(const QVector<QuickActionItem>& items);

    /**
     * Validates and copies a user-selected image file into the app-managed
     * quick_action_icons directory (scaled proportionally to <= 128x128 PNG).
     * Returns the managed destination path on success, or empty string on failure.
     */
    [[nodiscard]] static QString importCustomIcon(const QString& sourceImagePath,
                                                  const QString& shortcutId,
                                                  QString* errorOut = nullptr);

    /** Resolves the display QIcon for a shortcut (custom icon -> built-in/native icon -> fallback). */
    [[nodiscard]] static QIcon resolveIcon(const QuickActionItem& item, int logicalSize = 24);
};
