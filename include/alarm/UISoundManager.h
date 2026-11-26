#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QSoundEffect;

/**
 * Centralized, lightweight UI button interaction sound manager.
 * Loads the bundled Soft Pixel Pop WAV once via Qt Multimedia (QSoundEffect),
 * prevents overlapping playback and rapid-click floods via lightweight throttling,
 * and handles unavailable audio devices gracefully without blocking the GUI thread.
 */
class UISoundManager : public QObject {
    Q_OBJECT

public:
    static constexpr double kDefaultVolume = 0.20;
    static constexpr qint64 kMinClickIntervalMs = 45;

    explicit UISoundManager(QObject* parent = nullptr);
    ~UISoundManager() override;

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    void setVolume(double volume);
    [[nodiscard]] double volume() const { return m_volume; }

    /** Plays the cached button click sound once if enabled and outside the throttle window. */
    void playClick();

    /** Returns true when the underlying QSoundEffect loaded without error. */
    [[nodiscard]] bool isLoaded() const;

    /** Total number of accepted playClick() triggers (useful for deterministic testing). */
    [[nodiscard]] int playCount() const { return m_playCount; }
    void resetPlayCount() { m_playCount = 0; }

    [[nodiscard]] static QString resourceUrl();

private:
    void ensureLoaded();

    QSoundEffect* m_clickEffect = nullptr;
    QElapsedTimer m_lastPlayTimer;
    bool m_enabled = true;
    double m_volume = kDefaultVolume;
    int m_playCount = 0;
};
