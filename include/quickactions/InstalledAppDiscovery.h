#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

/**
 * Lightweight, cached, non-blocking installed-application discovery for
 * Chiikawa's Quick Actions picker.
 *
 * - Windows: scans Start Menu (.lnk) shortcuts and HKCU/HKLM App Paths registry
 *   registrations without recursively scanning entire drives.
 * - macOS: scans /Applications, /System/Applications, and ~/Applications for
 *   .app bundles.
 * - Never executes applications during discovery.
 * - Marks recognizable user-facing apps as suggested using shortcut location,
 *   resolved target, and file metadata; maintenance and system utilities stay
 *   searchable but are not suggested.
 * - Collects plain string metadata off the UI thread so QIcon extraction always
 *   runs safely on the GUI thread.
 */
class InstalledAppDiscovery {
public:
    struct DetectedApp {
        QString displayName;
        QString launchReference;
        QString subtitle;
        bool suggested = true;
        /** Resolved executable or bundle (shortcut target when known). */
        QString targetPath;

        [[nodiscard]] bool operator==(const DetectedApp& other) const = default;
    };

    using ProviderFn = std::function<QVector<DetectedApp>()>;

    /** Synchronous discovery (uses cache unless forceRefresh is true). */
    [[nodiscard]] static QVector<DetectedApp> discover(bool forceRefresh = false);

    /**
     * Non-blocking discovery: invokes `onReady` on `receiver`'s thread once
     * cached or freshly scanned results are available.
     */
    static void discoverAsync(QObject* receiver,
                              std::function<void(const QVector<DetectedApp>&)> onReady,
                              bool forceRefresh = false);

    /** Clears the in-memory discovery cache. */
    static void clearCache();

    /** Injects a custom provider for deterministic unit tests (pass {} to restore platform default). */
    static void setTestProvider(ProviderFn provider);

    /**
     * Filters detected apps by display name or location (case-insensitive).
     * An empty query returns suggested apps only unless showAll is true; a
     * non-empty query always searches every app, listing suggested matches first.
     */
    [[nodiscard]] static QVector<DetectedApp> filter(const QVector<DetectedApp>& apps,
                                                     const QString& query,
                                                     bool showAll = true);

    /** Heuristic used to decide whether an app is suggested by default. */
    [[nodiscard]] static bool isLikelyUserFacing(const DetectedApp& app);

private:
    [[nodiscard]] static QVector<DetectedApp> scanPlatformApps();
    [[nodiscard]] static QVector<DetectedApp> finalize(QVector<DetectedApp> apps);
};
