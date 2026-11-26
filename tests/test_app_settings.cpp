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
    void persistsQuickActionIconScaleAndHandlesInvalidValues();
    void persistsCustomQuickActionsAndValidatesMalformedData();

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

void TestAppSettings::persistsQuickActionIconScaleAndHandlesInvalidValues()
{
    clearSettings();
    {
        // Fresh user / upgrading user gets default 110%
        AppSettings s;
        QCOMPARE(s.quickActionIconScale(), SettingsDefaults::kQuickActionIconScalePercent);
        QCOMPARE(s.quickActionIconScale(), 110);

        // Clamping and 5% snapping
        s.setQuickActionIconScale(50);
        QCOMPARE(s.quickActionIconScale(), SettingsDefaults::kMinQuickActionIconScalePercent);
        s.setQuickActionIconScale(200);
        QCOMPARE(s.quickActionIconScale(), SettingsDefaults::kMaxQuickActionIconScalePercent);
        s.setQuickActionIconScale(127);
        QCOMPARE(s.quickActionIconScale(), 125);
        s.setQuickActionIconScale(135);

        // Changing icon scale must NOT change pet size
        s.setPetSize(160);
        s.save();
    }
    {
        AppSettings s;
        QCOMPARE(s.quickActionIconScale(), 135);
        QCOMPARE(s.petSize(), 160);

        // Restore Defaults resets quickActionIconScale to 110%
        s.restoreDefaults();
        QCOMPARE(s.quickActionIconScale(), SettingsDefaults::kQuickActionIconScalePercent);
    }
    {
        // Malformed stored value safely falls back to default
        QSettings raw(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
        raw.setValue(QStringLiteral("pet/quickActionIconScale"), QStringLiteral("not-a-number"));
        raw.sync();

        AppSettings s;
        QCOMPARE(s.quickActionIconScale(), SettingsDefaults::kQuickActionIconScalePercent);
    }
    clearSettings();
}

void TestAppSettings::persistsCustomQuickActionsAndValidatesMalformedData()
{
    clearSettings();
    {
        // Fresh/upgrading user gets the 6 default shortcuts
        AppSettings s;
        QCOMPARE(s.quickActions().size(), 6);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
        QCOMPARE(s.quickActions().at(5).id, QStringLiteral("builtin:exit"));

        // Reorder, disable Discord, and add a custom app
        QVector<QuickActionItem> items = s.quickActions();
        items[1].enabled = false; // Discord disabled
        QuickActionItem cursor = items.takeAt(3);
        items.prepend(cursor); // Cursor first
        items.push_back({QStringLiteral("custom:vscode"), QStringLiteral("VS Code"),
                         QuickActionType::CustomApp, QStringLiteral("C:/Apps/Code.exe"), {}, true});
        s.setQuickActions(items);
        s.setPetSize(150);
        s.save();
    }
    {
        // Reloaded after restart
        AppSettings s;
        QCOMPARE(s.quickActions().size(), 7);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:cursor"));
        QCOMPARE(s.quickActions().at(2).id, QStringLiteral("builtin:discord"));
        QCOMPARE(s.quickActions().at(2).enabled, false);
        QCOMPARE(s.quickActions().at(6).id, QStringLiteral("custom:vscode"));
        QCOMPARE(s.quickActions().at(6).applicationReference, QStringLiteral("C:/Apps/Code.exe"));

        // restoreDefaultQuickActions resets only shortcuts, preserving Pet Size
        s.restoreDefaultQuickActions();
        QCOMPARE(s.quickActions().size(), 6);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
        QCOMPARE(s.petSize(), 150);
    }
    {
        // Malformed JSON safely falls back to defaults
        QSettings raw(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
        raw.setValue(QStringLiteral("pet/quickActionsJson"), QByteArray("{bad json"));
        raw.sync();

        AppSettings s;
        QCOMPARE(s.quickActions().size(), 6);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
    }
    clearSettings();
}

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
