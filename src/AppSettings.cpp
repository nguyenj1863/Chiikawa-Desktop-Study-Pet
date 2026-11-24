#include "AppSettings.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr auto kOrg = "ChiikawaStudyPet";
constexpr auto kApp = "DesktopPet";

QString sanitizeSoundId(const QString& id)
{
    if (id == QStringLiteral("soft_chime")
        || id == QStringLiteral("tiny_bell")
        || id == QStringLiteral("cozy_pop")) {
        return id;
    }
    return SettingsDefaults::defaultAlarmSoundId();
}
} // namespace

AppSettings::AppSettings()
{
    load();
}

void AppSettings::load()
{
    QSettings settings(kOrg, kApp);

    m_petSize = settings.value(QStringLiteral("pet/size"), SettingsDefaults::kPetSize).toInt();
    m_movementSpeed = settings.value(QStringLiteral("pet/movementSpeed"), SettingsDefaults::kMoveSpeed).toDouble();
    m_movementEnabled = settings.value(QStringLiteral("pet/movementEnabled"), SettingsDefaults::kMovementEnabled).toBool();
    m_autoAnimationEnabled = settings.value(QStringLiteral("pet/autoAnimation"), SettingsDefaults::kAutoAnimationEnabled).toBool();

    if (settings.contains(QStringLiteral("pet/desktopOnlyMode"))) {
        m_desktopOnlyMode = settings.value(QStringLiteral("pet/desktopOnlyMode"), SettingsDefaults::kDesktopOnlyMode).toBool();
    } else if (settings.contains(QStringLiteral("pet/alwaysOnTop"))) {
        m_desktopOnlyMode = !settings.value(QStringLiteral("pet/alwaysOnTop"), true).toBool();
    } else {
        m_desktopOnlyMode = SettingsDefaults::kDesktopOnlyMode;
    }

    m_launchOnStartup = settings.value(QStringLiteral("app/launchOnStartup"), SettingsDefaults::kLaunchOnStartup).toBool();
    m_lastTimerSeconds = settings.value(QStringLiteral("timer/lastDuration"), SettingsDefaults::kTimerSeconds).toInt();

    m_soundEnabled = settings.value(QStringLiteral("app/soundEnabled"), SettingsDefaults::kSoundEnabled).toBool();
    m_alarmSoundId = settings.value(QStringLiteral("alarm/soundId"), SettingsDefaults::defaultAlarmSoundId()).toString();
    m_alarmVolume = settings.value(QStringLiteral("alarm/volume"), SettingsDefaults::kAlarmVolume).toDouble();
    m_alarmNotificationEnabled = settings.value(QStringLiteral("alarm/notificationEnabled"), SettingsDefaults::kAlarmNotificationEnabled).toBool();
    m_snoozeMinutes = settings.value(QStringLiteral("alarm/snoozeMinutes"), SettingsDefaults::kSnoozeMinutes).toInt();
    m_alarmUse24Hour = settings.value(QStringLiteral("alarm/use24Hour"), SettingsDefaults::kAlarmUse24Hour).toBool();

    const bool migratedOneTime = settings.value(QStringLiteral("alarm/oneTimeMode"), false).toBool();
    m_alarmConfigured = settings.value(QStringLiteral("alarm/configured"), false).toBool();
    m_alarmEnabled = settings.value(QStringLiteral("alarm/enabled"), false).toBool();
    const QString timeStr = settings.value(QStringLiteral("alarm/time")).toString();
    m_alarmTime = QTime::fromString(timeStr, QStringLiteral("HH:mm"));
    if (!m_alarmTime.isValid()) {
        m_alarmConfigured = false;
        m_alarmEnabled = false;
    }
    m_alarmNextTriggerUtcMs = settings.value(QStringLiteral("alarm/nextTriggerUtcMs"), 0).toLongLong();
    m_alarmState = settings.value(QStringLiteral("alarm/state"), 0).toInt();
    m_snoozeTargetUtcMs = settings.value(QStringLiteral("alarm/snoozeTargetUtcMs"), 0).toLongLong();
    m_lastTriggeredOccurrenceUtcMs = settings.value(QStringLiteral("alarm/lastTriggeredOccurrenceUtcMs"), 0).toLongLong();

    // Safely migrate legacy daily-alarm configurations without accidentally activating them
    if (!migratedOneTime && (m_alarmEnabled || m_alarmNextTriggerUtcMs > 0 || m_snoozeTargetUtcMs > 0)) {
        m_alarmEnabled = false;
        m_alarmNextTriggerUtcMs = 0;
        m_alarmState = 0;
        m_snoozeTargetUtcMs = 0;
    }

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
    settings.setValue(QStringLiteral("pet/desktopOnlyMode"), m_desktopOnlyMode);
    settings.setValue(QStringLiteral("pet/alwaysOnTop"), !m_desktopOnlyMode);
    settings.setValue(QStringLiteral("app/launchOnStartup"), m_launchOnStartup);
    settings.setValue(QStringLiteral("timer/lastDuration"), m_lastTimerSeconds);

    settings.setValue(QStringLiteral("app/soundEnabled"), m_soundEnabled);
    settings.setValue(QStringLiteral("alarm/soundId"), m_alarmSoundId);
    settings.setValue(QStringLiteral("alarm/volume"), m_alarmVolume);
    settings.setValue(QStringLiteral("alarm/notificationEnabled"), m_alarmNotificationEnabled);
    settings.setValue(QStringLiteral("alarm/snoozeMinutes"), m_snoozeMinutes);
    settings.setValue(QStringLiteral("alarm/use24Hour"), m_alarmUse24Hour);
    settings.setValue(QStringLiteral("alarm/oneTimeMode"), true);

    settings.setValue(QStringLiteral("alarm/configured"), m_alarmConfigured);
    settings.setValue(QStringLiteral("alarm/enabled"), m_alarmEnabled);
    if (m_alarmTime.isValid()) {
        settings.setValue(QStringLiteral("alarm/time"), m_alarmTime.toString(QStringLiteral("HH:mm")));
    } else {
        settings.remove(QStringLiteral("alarm/time"));
    }
    settings.setValue(QStringLiteral("alarm/nextTriggerUtcMs"), m_alarmNextTriggerUtcMs);
    settings.setValue(QStringLiteral("alarm/state"), m_alarmState);
    settings.setValue(QStringLiteral("alarm/snoozeTargetUtcMs"), m_snoozeTargetUtcMs);
    settings.setValue(QStringLiteral("alarm/lastTriggeredOccurrenceUtcMs"), m_lastTriggeredOccurrenceUtcMs);

    if (m_hasSavedPosition) {
        settings.setValue(QStringLiteral("pet/posX"), m_lastPosition.x());
        settings.setValue(QStringLiteral("pet/posY"), m_lastPosition.y());
    }

    settings.sync();
}

void AppSettings::restoreDefaults()
{
    m_petSize = SettingsDefaults::kPetSize;
    m_movementSpeed = SettingsDefaults::kMoveSpeed;
    m_movementEnabled = SettingsDefaults::kMovementEnabled;
    m_autoAnimationEnabled = SettingsDefaults::kAutoAnimationEnabled;
    m_desktopOnlyMode = SettingsDefaults::kDesktopOnlyMode;
    m_soundEnabled = SettingsDefaults::kSoundEnabled;
    m_alarmSoundId = SettingsDefaults::defaultAlarmSoundId();
    m_alarmVolume = SettingsDefaults::kAlarmVolume;
    m_alarmNotificationEnabled = SettingsDefaults::kAlarmNotificationEnabled;
    m_snoozeMinutes = SettingsDefaults::kSnoozeMinutes;
    m_alarmUse24Hour = SettingsDefaults::kAlarmUse24Hour;
    clampValues();
    save();
}

void AppSettings::setPetSize(int size)
{
    m_petSize = std::clamp(size, SettingsDefaults::kMinPetSize, SettingsDefaults::kMaxPetSize);
}

void AppSettings::setMovementSpeed(double speed)
{
    if (!std::isfinite(speed)) {
        m_movementSpeed = SettingsDefaults::kMoveSpeed;
        return;
    }
    m_movementSpeed = std::clamp(speed, SettingsDefaults::kMinMoveSpeed, SettingsDefaults::kMaxMoveSpeed);
}

void AppSettings::setMovementEnabled(bool enabled)
{
    m_movementEnabled = enabled;
}

void AppSettings::setAutoAnimationEnabled(bool enabled)
{
    m_autoAnimationEnabled = enabled;
}

void AppSettings::setDesktopOnlyMode(bool enabled)
{
    m_desktopOnlyMode = enabled;
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
    m_lastTimerSeconds = std::clamp(seconds, SettingsDefaults::kMinTimerSeconds, SettingsDefaults::kMaxTimerSeconds);
}

void AppSettings::setSoundEnabled(bool enabled)
{
    m_soundEnabled = enabled;
}

void AppSettings::setAlarmSoundId(const QString& soundId)
{
    m_alarmSoundId = sanitizeSoundId(soundId);
}

void AppSettings::setAlarmVolume(double volume)
{
    if (!std::isfinite(volume)) {
        m_alarmVolume = SettingsDefaults::kAlarmVolume;
        return;
    }
    m_alarmVolume = std::clamp(volume, SettingsDefaults::kMinAlarmVolume, SettingsDefaults::kMaxAlarmVolume);
}

void AppSettings::setAlarmNotificationEnabled(bool enabled)
{
    m_alarmNotificationEnabled = enabled;
}

void AppSettings::setSnoozeMinutes(int minutes)
{
    m_snoozeMinutes = SettingsDefaults::clampSnoozeMinutes(minutes);
}

void AppSettings::setAlarmUse24Hour(bool use24Hour)
{
    m_alarmUse24Hour = use24Hour;
}

void AppSettings::setAlarmConfigured(bool configured)
{
    m_alarmConfigured = configured;
}

void AppSettings::setAlarmEnabled(bool enabled)
{
    m_alarmEnabled = enabled;
}

void AppSettings::setAlarmTime(const QTime& time)
{
    m_alarmTime = time;
    if (time.isValid()) {
        m_alarmConfigured = true;
    }
}

void AppSettings::setAlarmNextTriggerUtcMs(qint64 utcMs)
{
    m_alarmNextTriggerUtcMs = std::max<qint64>(0, utcMs);
}

void AppSettings::setAlarmState(int state)
{
    m_alarmState = state;
}

void AppSettings::setSnoozeTargetUtcMs(qint64 utcMs)
{
    m_snoozeTargetUtcMs = std::max<qint64>(0, utcMs);
}

void AppSettings::setLastTriggeredOccurrenceUtcMs(qint64 utcMs)
{
    m_lastTriggeredOccurrenceUtcMs = std::max<qint64>(0, utcMs);
}

void AppSettings::clampValues()
{
    m_petSize = std::clamp(m_petSize, SettingsDefaults::kMinPetSize, SettingsDefaults::kMaxPetSize);
    if (!std::isfinite(m_movementSpeed)) {
        m_movementSpeed = SettingsDefaults::kMoveSpeed;
    }
    m_movementSpeed = std::clamp(m_movementSpeed, SettingsDefaults::kMinMoveSpeed, SettingsDefaults::kMaxMoveSpeed);
    m_lastTimerSeconds = std::clamp(m_lastTimerSeconds, SettingsDefaults::kMinTimerSeconds, SettingsDefaults::kMaxTimerSeconds);
    m_alarmSoundId = sanitizeSoundId(m_alarmSoundId);
    if (!std::isfinite(m_alarmVolume)) {
        m_alarmVolume = SettingsDefaults::kAlarmVolume;
    }
    m_alarmVolume = std::clamp(m_alarmVolume, SettingsDefaults::kMinAlarmVolume, SettingsDefaults::kMaxAlarmVolume);
    m_snoozeMinutes = SettingsDefaults::clampSnoozeMinutes(m_snoozeMinutes);
}
