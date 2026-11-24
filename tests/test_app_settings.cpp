#include "AppSettings.h"
#include "SettingsDefaults.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QTest>

class TestAppSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void clampsPetSize();
    void clampsSpeed();
    void persistsAndReloads();
    void validatesTimerDuration();
    void persistsDesktopAndAlarmPreferences();
    void restoreDefaultsPreservesActiveSessionsAndAlarms();

private:
    void clearSettings();
};

void TestAppSettings::clearSettings()
{
    QSettings settings(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    settings.clear();
    settings.sync();
}

void TestAppSettings::initTestCase()
{
    clearSettings();
}

void TestAppSettings::clampsPetSize()
{
    AppSettings s;
    s.setPetSize(10);
    QCOMPARE(s.petSize(), AppSettings::kMinPetSize);
    s.setPetSize(9999);
    QCOMPARE(s.petSize(), AppSettings::kMaxPetSize);
}

void TestAppSettings::clampsSpeed()
{
    AppSettings s;
    s.setMovementSpeed(0.01);
    QVERIFY(s.movementSpeed() >= 0.25);
    s.setMovementSpeed(100.0);
    QVERIFY(s.movementSpeed() <= 3.0);
}

void TestAppSettings::persistsAndReloads()
{
    clearSettings();
    {
        AppSettings s;
        s.setPetSize(180);
        s.setMovementSpeed(1.5);
        s.setMovementEnabled(false);
        s.setAutoAnimationEnabled(false);
        s.setAlwaysOnTop(false);
        s.setLastPosition(QPoint(42, 84));
        s.setLastTimerSeconds(45 * 60);
        s.setSoundEnabled(false);
        s.save();
    }
    {
        AppSettings s;
        QCOMPARE(s.petSize(), 180);
        QCOMPARE(s.movementSpeed(), 1.5);
        QCOMPARE(s.movementEnabled(), false);
        QCOMPARE(s.autoAnimationEnabled(), false);
        QCOMPARE(s.alwaysOnTop(), false);
        QCOMPARE(s.desktopOnlyMode(), true);
        QCOMPARE(s.lastPosition(), QPoint(42, 84));
        QVERIFY(s.hasSavedPosition());
        QCOMPARE(s.lastTimerSeconds(), 45 * 60);
        QCOMPARE(s.soundEnabled(), false);
    }
    clearSettings();
}

void TestAppSettings::validatesTimerDuration()
{
    AppSettings s;
    s.setLastTimerSeconds(0);
    QCOMPARE(s.lastTimerSeconds(), 1);
    s.setLastTimerSeconds(999999);
    QCOMPARE(s.lastTimerSeconds(), 24 * 60 * 60);
}

void TestAppSettings::persistsDesktopAndAlarmPreferences()
{
    clearSettings();
    const qint64 snoozeUtcMs = QDateTime(QDate(2026, 11, 24), QTime(17, 35, 0)).toMSecsSinceEpoch();
    {
        AppSettings s;
        s.setDesktopOnlyMode(true);
        s.setAlarmSoundId(QStringLiteral("tiny_bell"));
        s.setAlarmVolume(0.65);
        s.setAlarmNotificationEnabled(false);
        s.setSnoozeMinutes(15);
        s.setAlarmUse24Hour(true);
        s.setButtonSoundsEnabled(false);
        s.setAlarmConfigured(true);
        s.setAlarmEnabled(true);
        s.setAlarmTime(QTime(17, 30));
        s.setSnoozeTargetUtcMs(snoozeUtcMs);
        s.save();
    }
    {
        AppSettings s;
        QCOMPARE(s.desktopOnlyMode(), true);
        QCOMPARE(s.alarmSoundId(), QStringLiteral("tiny_bell"));
        QVERIFY(qAbs(s.alarmVolume() - 0.65) < 0.001);
        QCOMPARE(s.alarmNotificationEnabled(), false);
        QCOMPARE(s.snoozeMinutes(), 15);
        QCOMPARE(s.alarmUse24Hour(), true);
        QCOMPARE(s.buttonSoundsEnabled(), false);
        QCOMPARE(s.soundEnabled(), true); // independent of buttonSoundsEnabled
        QCOMPARE(s.hasAlarmConfigured(), true);
        QCOMPARE(s.alarmEnabled(), true);
        QCOMPARE(s.alarmTime(), QTime(17, 30));
        QCOMPARE(s.snoozeTargetUtcMs(), snoozeUtcMs);

        // Snooze clamping snaps to 5 / 10 / 15
        s.setSnoozeMinutes(99);
        QCOMPARE(s.snoozeMinutes(), 15);
        s.setSnoozeMinutes(1);
        QCOMPARE(s.snoozeMinutes(), 5);
    }
    clearSettings();
}

void TestAppSettings::restoreDefaultsPreservesActiveSessionsAndAlarms()
{
    clearSettings();
    const qint64 snoozeUtcMs = QDateTime(QDate(2026, 11, 24), QTime(17, 35, 0)).toMSecsSinceEpoch();
    AppSettings s;
    s.setPetSize(210);
    s.setMovementSpeed(2.2);
    s.setMovementEnabled(false);
    s.setAutoAnimationEnabled(false);
    s.setDesktopOnlyMode(true);
    s.setSoundEnabled(false);
    s.setButtonSoundsEnabled(false);
    s.setAlarmSoundId(QStringLiteral("cozy_pop"));
    s.setAlarmVolume(0.8);
    s.setAlarmNotificationEnabled(false);
    s.setSnoozeMinutes(15);
    s.setLastPosition(QPoint(300, 200));
    s.setAlarmConfigured(true);
    s.setAlarmEnabled(true);
    s.setAlarmTime(QTime(17, 30));
    s.setSnoozeTargetUtcMs(snoozeUtcMs);
    s.save();

    s.restoreDefaults();

    // Preferences reset to centralized defaults
    QCOMPARE(s.petSize(), SettingsDefaults::kPetSize);
    QCOMPARE(s.movementSpeed(), SettingsDefaults::kMoveSpeed);
    QCOMPARE(s.movementEnabled(), SettingsDefaults::kMovementEnabled);
    QCOMPARE(s.autoAnimationEnabled(), SettingsDefaults::kAutoAnimationEnabled);
    QCOMPARE(s.desktopOnlyMode(), SettingsDefaults::kDesktopOnlyMode);
    QCOMPARE(s.soundEnabled(), SettingsDefaults::kSoundEnabled);
    QCOMPARE(s.buttonSoundsEnabled(), SettingsDefaults::kButtonSoundsEnabled);
    QCOMPARE(s.alarmSoundId(), SettingsDefaults::defaultAlarmSoundId());
    QVERIFY(qAbs(s.alarmVolume() - SettingsDefaults::kAlarmVolume) < 0.001);
    QCOMPARE(s.alarmNotificationEnabled(), SettingsDefaults::kAlarmNotificationEnabled);
    QCOMPARE(s.snoozeMinutes(), SettingsDefaults::kSnoozeMinutes);

    // Active alarm, pending snooze, and saved position must be preserved
    QCOMPARE(s.alarmEnabled(), true);
    QCOMPARE(s.alarmTime(), QTime(17, 30));
    QCOMPARE(s.snoozeTargetUtcMs(), snoozeUtcMs);
    QCOMPARE(s.lastPosition(), QPoint(300, 200));

    clearSettings();
}

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
