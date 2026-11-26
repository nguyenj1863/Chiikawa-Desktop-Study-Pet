#pragma once

#include <QString>
#include <QUrl>
#include <array>
#include <functional>

/**
 * Reusable cross-platform launcher/activator for external desktop applications
 * accessed from Chiikawa's quick-action menu:
 * Spotify, Discord, League of Legends, and Cursor.
 *
 * - Activates an already-running window when supported by the OS.
 * - Launches installed desktop applications without shell string concatenation.
 * - Reports NotInstalled / LaunchFailed cleanly with an optional web fallback URL
 *   (never auto-opening a browser).
 * - Supports per-app dependency-injected backends for deterministic unit tests.
 */
class ExternalAppLauncher {
public:
    enum class AppId : int {
        Spotify = 0,
        Discord = 1,
        LeagueOfLegends = 2,
        Cursor = 3,
        Count = 4
    };

    enum class Status {
        ActivatedRunning,
        LaunchedApp,
        NotInstalled,
        LaunchFailed
    };

    struct AppMetadata {
        AppId id = AppId::Spotify;
        QString displayName;
        QString fallbackActionLabel;
        QUrl fallbackUrl;
    };

    struct Result {
        AppId app = AppId::Spotify;
        Status status = Status::LaunchFailed;
        QString title;
        QString message;
        QString fallbackActionLabel;
        QUrl fallbackUrl;
    };

    struct Backend {
        std::function<bool()> activateRunning;
        std::function<bool()> isInstalled;
        std::function<bool()> launchInstalled;
    };

    ExternalAppLauncher();

    void setBackend(AppId app, Backend backend);
    [[nodiscard]] Result openOrActivate(AppId app) const;

    [[nodiscard]] static AppMetadata metadata(AppId app);
    [[nodiscard]] static Backend defaultPlatformBackend(AppId app);

private:
    std::array<Backend, static_cast<int>(AppId::Count)> m_backends;
};
