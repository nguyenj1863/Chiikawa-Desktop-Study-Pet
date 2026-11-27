#pragma once

#include "quickactions/QuickActionItem.h"
#include "app/SettingsDefaults.h"

#include <QPoint>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

/**
 * Persistent application preferences and clock-alarm state via QSettings.
 * Values are validated on load; invalid data falls back to SettingsDefaults.
 */
class AppSettings {
public:
    static constexpr int kDefaultPetSize = SettingsDefaults::kPetSize;
    static constexpr int kMinPetSize = SettingsDefaults::kMinPetSize;
    static constexpr int kMaxPetSize = SettingsDefaults::kMaxPetSize;
    static constexpr double kDefaultMoveSpeed = SettingsDefaults::kMoveSpeed;
    static constexpr int kDefaultTimerSeconds = SettingsDefaults::kTimerSeconds;

    AppSettings();

    /**
     * Redirects persistence to a separate QSettings scope. Tests use this so
     * they never read, overwrite, or clear the user's real preferences.
     */
    static void setStorageScope(const QString& organization, const QString& application);
    [[nodiscard]] static QString storageOrganization();
    [[nodiscard]] static QString storageApplication();

    void load();
    void save() const;

    /**
     * Resets user preferences to SettingsDefaults while preserving
     * active study timer state, saved pet position, and configured clock alarms/snoozes.
     */
    void restoreDefaults();

    // --- Pet preferences ---
    [[nodiscard]] int petSize() const { return m_petSize; }
    void setPetSize(int size);

    [[nodiscard]] int quickActionIconScale() const { return m_quickActionIconScale; }
    void setQuickActionIconScale(int percent);

    [[nodiscard]] const QVector<QuickActionItem>& quickActions() const { return m_quickActions; }
    void setQuickActions(const QVector<QuickActionItem>& items);
    void restoreDefaultQuickActions();

    [[nodiscard]] double movementSpeed() const { return m_movementSpeed; }
    void setMovementSpeed(double speed);

    [[nodiscard]] bool movementEnabled() const { return m_movementEnabled; }
    void setMovementEnabled(bool enabled);

    [[nodiscard]] bool autoAnimationEnabled() const { return m_autoAnimationEnabled; }
    void setAutoAnimationEnabled(bool enabled);

    [[nodiscard]] bool desktopOnlyMode() const { return m_desktopOnlyMode; }
    void setDesktopOnlyMode(bool enabled);

    // Compatibility alias for existing callers/tests: floating mode is !desktopOnlyMode
    [[nodiscard]] bool alwaysOnTop() const { return !m_desktopOnlyMode; }
    void setAlwaysOnTop(bool enabled) { setDesktopOnlyMode(!enabled); }

    [[nodiscard]] bool launchOnStartup() const { return m_launchOnStartup; }
    void setLaunchOnStartup(bool enabled);

    [[nodiscard]] QPoint lastPosition() const { return m_lastPosition; }
    void setLastPosition(const QPoint& pos);
    [[nodiscard]] bool hasSavedPosition() const { return m_hasSavedPosition; }

    // --- Study timer ---
    [[nodiscard]] int lastTimerSeconds() const { return m_lastTimerSeconds; }
    void setLastTimerSeconds(int seconds);

    // --- Sound & alarm preferences ---
    [[nodiscard]] bool soundEnabled() const { return m_soundEnabled; }
    void setSoundEnabled(bool enabled);

    [[nodiscard]] bool buttonSoundsEnabled() const { return m_buttonSoundsEnabled; }
    void setButtonSoundsEnabled(bool enabled);

    // --- Onboarding ---
    [[nodiscard]] bool onboardingCompleted() const { return m_onboardingCompleted; }
    void setOnboardingCompleted(bool completed);

    [[nodiscard]] QString alarmSoundId() const { return m_alarmSoundId; }
    void setAlarmSoundId(const QString& soundId);

    [[nodiscard]] double alarmVolume() const { return m_alarmVolume; }
    void setAlarmVolume(double volume);

    [[nodiscard]] bool alarmNotificationEnabled() const { return m_alarmNotificationEnabled; }
    void setAlarmNotificationEnabled(bool enabled);

    [[nodiscard]] int snoozeMinutes() const { return m_snoozeMinutes; }
    void setSnoozeMinutes(int minutes);

    [[nodiscard]] bool alarmUse24Hour() const { return m_alarmUse24Hour; }
    void setAlarmUse24Hour(bool use24Hour);

    // --- Clock alarm configuration & recovery state ---
    [[nodiscard]] bool hasAlarmConfigured() const { return m_alarmConfigured; }
    void setAlarmConfigured(bool configured);

    [[nodiscard]] bool alarmEnabled() const { return m_alarmEnabled; }
    void setAlarmEnabled(bool enabled);

    [[nodiscard]] QTime alarmTime() const { return m_alarmTime; }
    void setAlarmTime(const QTime& time);

    [[nodiscard]] qint64 alarmNextTriggerUtcMs() const { return m_alarmNextTriggerUtcMs; }
    void setAlarmNextTriggerUtcMs(qint64 utcMs);

    [[nodiscard]] int alarmState() const { return m_alarmState; }
    void setAlarmState(int state);

    [[nodiscard]] qint64 snoozeTargetUtcMs() const { return m_snoozeTargetUtcMs; }
    void setSnoozeTargetUtcMs(qint64 utcMs);

    [[nodiscard]] qint64 lastTriggeredOccurrenceUtcMs() const { return m_lastTriggeredOccurrenceUtcMs; }
    void setLastTriggeredOccurrenceUtcMs(qint64 utcMs);

    // Random study check-ins ("Chiikawa asks me questions"). Restore Defaults resets these.
    [[nodiscard]] bool checkInsEnabled() const { return m_checkInsEnabled; }
    void setCheckInsEnabled(bool enabled);

    [[nodiscard]] int checkInMinutes() const { return m_checkInMinutes; }
    void setCheckInMinutes(int minutes);

    [[nodiscard]] QStringList recentQuestionIds() const { return m_recentQuestionIds; }
    void setRecentQuestionIds(const QStringList& ids);

    // Study preferences. Restore Defaults resets formats but keeps
    // the selected study set, the model, and all Study Library data and progress.
    [[nodiscard]] qint64 studySetId() const { return m_studySetId; }
    void setStudySetId(qint64 id);

    /** Bit mask of SettingsDefaults::kStudyFormat* values; never empty. */
    [[nodiscard]] int studyFormats() const { return m_studyFormats; }
    /** Ignores an empty mask so at least one format always stays enabled; returns whether it applied. */
    bool setStudyFormats(int mask);

    [[nodiscard]] int studyFormatMode() const { return m_studyFormatMode; }
    void setStudyFormatMode(int mode);

    [[nodiscard]] int studyFocusFormat() const { return m_studyFocusFormat; }
    void setStudyFocusFormat(int format);

    [[nodiscard]] QString studyModel() const { return m_studyModel; }
    void setStudyModel(const QString& model);

private:
    void clampValues();

    int m_petSize = SettingsDefaults::kPetSize;
    int m_quickActionIconScale = SettingsDefaults::kQuickActionIconScalePercent;
    QVector<QuickActionItem> m_quickActions;
    double m_movementSpeed = SettingsDefaults::kMoveSpeed;
    bool m_movementEnabled = SettingsDefaults::kMovementEnabled;
    bool m_autoAnimationEnabled = SettingsDefaults::kAutoAnimationEnabled;
    bool m_desktopOnlyMode = SettingsDefaults::kDesktopOnlyMode;
    bool m_launchOnStartup = SettingsDefaults::kLaunchOnStartup;
    QPoint m_lastPosition;
    bool m_hasSavedPosition = false;
    int m_lastTimerSeconds = SettingsDefaults::kTimerSeconds;

    bool m_soundEnabled = SettingsDefaults::kSoundEnabled;
    bool m_buttonSoundsEnabled = SettingsDefaults::kButtonSoundsEnabled;
    bool m_onboardingCompleted = SettingsDefaults::kOnboardingCompleted;
    QString m_alarmSoundId = SettingsDefaults::defaultAlarmSoundId();
    double m_alarmVolume = SettingsDefaults::kAlarmVolume;
    bool m_alarmNotificationEnabled = SettingsDefaults::kAlarmNotificationEnabled;
    int m_snoozeMinutes = SettingsDefaults::kSnoozeMinutes;
    bool m_alarmUse24Hour = SettingsDefaults::kAlarmUse24Hour;

    bool m_alarmConfigured = false;
    bool m_alarmEnabled = false;
    QTime m_alarmTime;
    qint64 m_alarmNextTriggerUtcMs = 0;
    int m_alarmState = 0;
    qint64 m_snoozeTargetUtcMs = 0;
    qint64 m_lastTriggeredOccurrenceUtcMs = 0;

    bool m_checkInsEnabled = SettingsDefaults::kCheckInsEnabled;
    int m_checkInMinutes = SettingsDefaults::kCheckInMinutes;
    QStringList m_recentQuestionIds;

    qint64 m_studySetId = 0;
    int m_studyFormats = SettingsDefaults::kStudyFormatsMask;
    int m_studyFormatMode = SettingsDefaults::kStudyFormatMode;
    int m_studyFocusFormat = SettingsDefaults::kStudyFocusFormat;
    QString m_studyModel = SettingsDefaults::defaultStudyModel();
};
