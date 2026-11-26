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
 * - Collects plain string metadata off the UI thread so QIcon extraction always
 *   runs safely on the GUI thread.
 */
class InstalledAppDiscovery {
public:
    struct DetectedApp {
        QString displayName;
        QString launchReference;
        QString subtitle;

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

    /** Filters detected apps case-insensitively by display name or subtitle. */
    [[nodiscard]] static QVector<DetectedApp> filter(const QVector<DetectedApp>& apps,
                                                     const QString& query);

private:
    [[nodiscard]] static QVector<DetectedApp> scanPlatformApps();
    [[nodiscard]] static QVector<DetectedApp> deduplicateAndSort(QVector<DetectedApp> apps);
};
