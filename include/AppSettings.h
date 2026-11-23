#pragma once

#include <QPoint>
#include <QSettings>
#include <QString>

/**
 * Persistent application preferences via QSettings.
 * Values are validated on load; invalid data falls back to safe defaults.
 */
class AppSettings {
public:
    static constexpr int kDefaultPetSize = 120;
    static constexpr int kMinPetSize = 64;
    static constexpr int kMaxPetSize = 256;
    static constexpr double kDefaultMoveSpeed = 1.0;
    static constexpr int kDefaultTimerSeconds = 25 * 60;

    AppSettings();

    void load();
    void save() const;

    [[nodiscard]] int petSize() const { return m_petSize; }
    void setPetSize(int size);

    [[nodiscard]] double movementSpeed() const { return m_movementSpeed; }
    void setMovementSpeed(double speed);

    [[nodiscard]] bool movementEnabled() const { return m_movementEnabled; }
    void setMovementEnabled(bool enabled);

    [[nodiscard]] bool autoAnimationEnabled() const { return m_autoAnimationEnabled; }
    void setAutoAnimationEnabled(bool enabled);

    [[nodiscard]] bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool enabled);

    [[nodiscard]] bool launchOnStartup() const { return m_launchOnStartup; }
    void setLaunchOnStartup(bool enabled);

    [[nodiscard]] QPoint lastPosition() const { return m_lastPosition; }
    void setLastPosition(const QPoint& pos);

    [[nodiscard]] bool hasSavedPosition() const { return m_hasSavedPosition; }

    [[nodiscard]] int lastTimerSeconds() const { return m_lastTimerSeconds; }
    void setLastTimerSeconds(int seconds);

    [[nodiscard]] bool soundEnabled() const { return m_soundEnabled; }
    void setSoundEnabled(bool enabled);

private:
    void clampValues();

    int m_petSize = kDefaultPetSize;
    double m_movementSpeed = kDefaultMoveSpeed;
    bool m_movementEnabled = true;
    bool m_autoAnimationEnabled = true;
    bool m_alwaysOnTop = true;
    bool m_launchOnStartup = false;
    QPoint m_lastPosition;
    bool m_hasSavedPosition = false;
    int m_lastTimerSeconds = kDefaultTimerSeconds;
    bool m_soundEnabled = true;
};
