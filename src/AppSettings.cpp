#include "AppSettings.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr auto kOrg = "ChiikawaStudyPet";
constexpr auto kApp = "DesktopPet";
}

AppSettings::AppSettings()
{
    load();
}

void AppSettings::load()
{
    QSettings settings(kOrg, kApp);

    m_petSize = settings.value(QStringLiteral("pet/size"), kDefaultPetSize).toInt();
    m_movementSpeed = settings.value(QStringLiteral("pet/movementSpeed"), kDefaultMoveSpeed).toDouble();
    m_movementEnabled = settings.value(QStringLiteral("pet/movementEnabled"), true).toBool();
    m_autoAnimationEnabled = settings.value(QStringLiteral("pet/autoAnimation"), true).toBool();
    m_alwaysOnTop = settings.value(QStringLiteral("pet/alwaysOnTop"), true).toBool();
    m_launchOnStartup = settings.value(QStringLiteral("app/launchOnStartup"), false).toBool();
    m_lastTimerSeconds = settings.value(QStringLiteral("timer/lastDuration"), kDefaultTimerSeconds).toInt();
    m_soundEnabled = settings.value(QStringLiteral("app/soundEnabled"), true).toBool();

    if (settings.contains(QStringLiteral("pet/posX")) && settings.contains(QStringLiteral("pet/posY"))) {
        m_lastPosition = QPoint(
            settings.value(QStringLiteral("pet/posX")).toInt(),
            settings.value(QStringLiteral("pet/posY")).toInt());
        m_hasSavedPosition = true;
    } else {
        m_hasSavedPosition = false;
    }

    clampValues();
}

void AppSettings::save() const
{
    QSettings settings(kOrg, kApp);

    settings.setValue(QStringLiteral("pet/size"), m_petSize);
    settings.setValue(QStringLiteral("pet/movementSpeed"), m_movementSpeed);
    settings.setValue(QStringLiteral("pet/movementEnabled"), m_movementEnabled);
    settings.setValue(QStringLiteral("pet/autoAnimation"), m_autoAnimationEnabled);
    settings.setValue(QStringLiteral("pet/alwaysOnTop"), m_alwaysOnTop);
    settings.setValue(QStringLiteral("app/launchOnStartup"), m_launchOnStartup);
    settings.setValue(QStringLiteral("timer/lastDuration"), m_lastTimerSeconds);
    settings.setValue(QStringLiteral("app/soundEnabled"), m_soundEnabled);

    if (m_hasSavedPosition) {
        settings.setValue(QStringLiteral("pet/posX"), m_lastPosition.x());
        settings.setValue(QStringLiteral("pet/posY"), m_lastPosition.y());
    }

    settings.sync();
}

void AppSettings::setPetSize(int size)
{
    m_petSize = std::clamp(size, kMinPetSize, kMaxPetSize);
}

void AppSettings::setMovementSpeed(double speed)
{
    m_movementSpeed = std::clamp(speed, 0.25, 3.0);
}

void AppSettings::setMovementEnabled(bool enabled)
{
    m_movementEnabled = enabled;
}

void AppSettings::setAutoAnimationEnabled(bool enabled)
{
    m_autoAnimationEnabled = enabled;
}

void AppSettings::setAlwaysOnTop(bool enabled)
{
    m_alwaysOnTop = enabled;
}

void AppSettings::setLaunchOnStartup(bool enabled)
{
    m_launchOnStartup = enabled;
}

void AppSettings::setLastPosition(const QPoint& pos)
{
    m_lastPosition = pos;
    m_hasSavedPosition = true;
}

void AppSettings::setLastTimerSeconds(int seconds)
{
    m_lastTimerSeconds = std::clamp(seconds, 1, 24 * 60 * 60);
}

void AppSettings::setSoundEnabled(bool enabled)
{
    m_soundEnabled = enabled;
}

void AppSettings::clampValues()
{
    m_petSize = std::clamp(m_petSize, kMinPetSize, kMaxPetSize);
    if (!std::isfinite(m_movementSpeed)) {
        m_movementSpeed = kDefaultMoveSpeed;
    }
    m_movementSpeed = std::clamp(m_movementSpeed, 0.25, 3.0);
    m_lastTimerSeconds = std::clamp(m_lastTimerSeconds, 1, 24 * 60 * 60);
}
