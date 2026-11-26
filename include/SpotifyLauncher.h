#pragma once

#include <QString>
#include <QUrl>
#include <functional>

/**
 * Cross-platform launcher/activator for the Spotify desktop application.
 *
 * - Brings an already-running Spotify window to the foreground when possible.
 * - Launches installed Spotify (Windows standalone & Microsoft Store, macOS app bundle).
 * - Reports NotInstalled cleanly without launching a web browser automatically.
 * - Supports dependency-injected backend callbacks for deterministic unit testing.
 */
class SpotifyLauncher {
public:
    enum class Status {
        ActivatedRunning,
        LaunchedApp,
        NotInstalled,
        LaunchFailed
    };

    struct Result {
        Status status = Status::LaunchFailed;
        QString message;
    };

    struct Backend {
        std::function<bool()> activateRunning;
        std::function<bool()> isInstalled;
        std::function<bool()> launchInstalled;
    };

    SpotifyLauncher();
    explicit SpotifyLauncher(Backend backend);

    [[nodiscard]] Result openOrActivate() const;

    [[nodiscard]] static QUrl webPlayerUrl()
    {
        return QUrl(QStringLiteral("https://open.spotify.com"));
    }

    [[nodiscard]] static Backend defaultPlatformBackend();

private:
    Backend m_backend;
};
