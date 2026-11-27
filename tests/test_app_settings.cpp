#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTest>

namespace {
[[maybe_unused]] const bool kIsolatedSettingsStore = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetTests"));
    return true;
}();
} // namespace

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
    void migratesStoredShortcutsToVsCodeAndStudy();
    void migrationKeepsCustomizedNamesAndRespectsTheRadialLimit();
    void migrationDoesNothingToCurrentListsOrMalformedEntries();

private:
    void clearSettings();
};

void TestAppSettings::clearSettings()
{
    QSettings settings(AppSettings::storageOrganization(), AppSettings::storageApplication());
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
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
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
        // Fresh user gets the 7 default shortcuts
        AppSettings s;
        QCOMPARE(s.quickActions().size(), 7);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
        QCOMPARE(s.quickActions().at(4).id, QStringLiteral("builtin:study"));
        QCOMPARE(s.quickActions().at(5).id, QStringLiteral("builtin:settings"));
        QCOMPARE(s.quickActions().at(6).id, QStringLiteral("builtin:exit"));

        // Reorder, disable Discord, and add a custom app
        QVector<QuickActionItem> items = s.quickActions();
        items[1].enabled = false; // Discord disabled
        QuickActionItem vscode = items.takeAt(3);
        items.prepend(vscode); // VS Code first
        items.push_back({QStringLiteral("custom:editor"), QStringLiteral("My Editor"),
                         QuickActionType::CustomApp, QStringLiteral("C:/Apps/Code.exe"), {}, true});
        s.setQuickActions(items);
        s.setPetSize(150);
        s.save();
    }
    {
        // Reloaded after restart
        AppSettings s;
        QCOMPARE(s.quickActions().size(), 8);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:vscode"));
        QCOMPARE(s.quickActions().at(2).id, QStringLiteral("builtin:discord"));
        QCOMPARE(s.quickActions().at(2).enabled, false);
        QCOMPARE(s.quickActions().at(7).id, QStringLiteral("custom:editor"));
        QCOMPARE(s.quickActions().at(7).applicationReference, QStringLiteral("C:/Apps/Code.exe"));

        // restoreDefaultQuickActions resets only shortcuts, preserving Pet Size
        s.restoreDefaultQuickActions();
        QCOMPARE(s.quickActions().size(), 7);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
        QCOMPARE(s.petSize(), 150);
    }
    {
        // Malformed JSON safely falls back to defaults
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        raw.setValue(QStringLiteral("pet/quickActionsJson"), QByteArray("{bad json"));
        raw.sync();

        AppSettings s;
        QCOMPARE(s.quickActions().size(), 7);
        QCOMPARE(s.quickActions().at(0).id, QStringLiteral("builtin:spotify"));
    }
    clearSettings();
}

namespace {
QByteArray storedShortcuts(const QJsonArray& array)
{
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QJsonObject stored(const QString& id, const QString& name, const QString& type, const QString& ref = QString(),
                   bool enabled = true, const QString& icon = QString())
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("type"), type},
                       {QStringLiteral("appRef"), ref}, {QStringLiteral("icon"), icon}, {QStringLiteral("enabled"), enabled}};
}

QJsonArray version1Defaults()
{
    return QJsonArray{
        stored(QStringLiteral("builtin:spotify"), QStringLiteral("Spotify"), QStringLiteral("builtin_app"), QStringLiteral("spotify")),
        stored(QStringLiteral("builtin:discord"), QStringLiteral("Discord"), QStringLiteral("builtin_app"), QStringLiteral("discord")),
        stored(QStringLiteral("builtin:league"), QStringLiteral("League of Legends"), QStringLiteral("builtin_app"), QStringLiteral("league")),
        stored(QStringLiteral("builtin:cursor"), QStringLiteral("Cursor"), QStringLiteral("builtin_app"), QStringLiteral("cursor")),
        stored(QStringLiteral("builtin:settings"), QStringLiteral("Settings"), QStringLiteral("settings")),
        stored(QStringLiteral("builtin:exit"), QStringLiteral("Exit"), QStringLiteral("exit")),
    };
}
} // namespace

void TestAppSettings::migratesStoredShortcutsToVsCodeAndStudy()
{
    clearSettings();
    {
        // A version 1 list (no version key): Cursor becomes VS Code in place; Study lands above Settings.
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        QJsonArray v1 = version1Defaults();
        QJsonObject discord = v1.at(1).toObject();
        discord.insert(QStringLiteral("enabled"), false);
        v1.replace(1, discord);
        QJsonObject cursor = v1.at(3).toObject();
        cursor.insert(QStringLiteral("enabled"), false);
        v1.replace(3, cursor);
        v1.append(stored(QStringLiteral("custom:cursor"), QStringLiteral("My Cursor"), QStringLiteral("custom_app"),
                         QStringLiteral("C:/Apps/Cursor.exe")));
        raw.setValue(QStringLiteral("pet/quickActionsJson"), storedShortcuts(v1));
        raw.sync();
    }
    {
        AppSettings s;
        const QVector<QuickActionItem> items = s.quickActions();
        QStringList ids;
        for (const QuickActionItem& i : items) {
            ids.append(i.id);
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("builtin:spotify"), QStringLiteral("builtin:discord"),
                                   QStringLiteral("builtin:league"), QStringLiteral("builtin:vscode"),
                                   QStringLiteral("builtin:study"), QStringLiteral("builtin:settings"),
                                   QStringLiteral("builtin:exit"), QStringLiteral("custom:cursor")}));
        QCOMPARE(items.at(3).displayName, QStringLiteral("Visual Studio Code"));
        QCOMPARE(items.at(3).applicationReference, QStringLiteral("vscode"));
        QVERIFY(!items.at(3).enabled);        // the old Cursor entry's enabled state is kept
        QVERIFY(!items.at(1).enabled);        // other shortcuts keep their state and order
        QVERIFY(items.at(4).enabled);
        QCOMPARE(items.at(7).type, QuickActionType::CustomApp);   // user-created Cursor entry untouched
        QCOMPARE(items.at(7).applicationReference, QStringLiteral("C:/Apps/Cursor.exe"));
        QCOMPARE(items.at(7).displayName, QStringLiteral("My Cursor"));
        s.save();
    }
    {
        // Migrated once: reloading does not insert a second Study shortcut or change anything.
        AppSettings s;
        QCOMPARE(s.quickActions().size(), 8);
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        QCOMPARE(raw.value(QStringLiteral("pet/quickActionsVersion")).toInt(), QuickActionItem::kStoredVersion);
    }
    clearSettings();
}

void TestAppSettings::migrationKeepsCustomizedNamesAndRespectsTheRadialLimit()
{
    clearSettings();
    {
        QJsonArray v1 = version1Defaults();
        // The user renamed the built-in Cursor shortcut and gave it a custom icon: both are kept.
        v1.replace(3, stored(QStringLiteral("builtin:cursor"), QStringLiteral("Editor"), QStringLiteral("builtin_app"),
                             QStringLiteral("cursor"), true, QStringLiteral("C:/icons/editor.png")));
        // Fill the radial menu: 6 built-ins plus 4 custom apps = 10 enabled shortcuts.
        for (int i = 0; i < 4; ++i) {
            v1.append(stored(QStringLiteral("custom:app%1").arg(i), QStringLiteral("App %1").arg(i),
                             QStringLiteral("custom_app"), QStringLiteral("C:/Apps/app%1.exe").arg(i)));
        }
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        raw.setValue(QStringLiteral("pet/quickActionsJson"), storedShortcuts(v1));
        raw.sync();
    }
    AppSettings s;
    const QVector<QuickActionItem> items = s.quickActions();
    QCOMPARE(items.size(), 11);
    QCOMPARE(items.at(3).id, QStringLiteral("builtin:vscode"));
    QCOMPARE(items.at(3).displayName, QStringLiteral("Editor"));
    QCOMPARE(items.at(3).customIconPath, QStringLiteral("C:/icons/editor.png"));
    QCOMPARE(items.at(4).id, QStringLiteral("builtin:study"));
    QVERIFY(!items.at(4).enabled);   // 10 were already enabled, so Study arrives disabled instead of pushing one out
    int enabled = 0;
    for (const QuickActionItem& i : items) {
        enabled += i.enabled ? 1 : 0;
    }
    QCOMPARE(enabled, 10);
    clearSettings();
}

void TestAppSettings::migrationDoesNothingToCurrentListsOrMalformedEntries()
{
    QJsonArray current = QuickActionItem::toJsonArray(QuickActionItem::defaultShortcuts());
    QJsonArray copy = current;
    QVERIFY(!QuickActionItem::migrateStoredList(copy, QuickActionItem::kStoredVersion));
    QCOMPARE(copy, current);

    // Running the upgrade twice never duplicates Study.
    QJsonArray v1 = version1Defaults();
    QVERIFY(QuickActionItem::migrateStoredList(v1, 1));
    QJsonArray again = v1;
    QVERIFY(!QuickActionItem::migrateStoredList(again, 1));
    QCOMPARE(again, v1);

    // Non-object entries are left for the parser to drop; they do not crash the upgrade.
    QJsonArray odd{QJsonValue(5), stored(QStringLiteral("builtin:settings"), QStringLiteral("Settings"), QStringLiteral("settings"))};
    QVERIFY(QuickActionItem::migrateStoredList(odd, 1));
    QCOMPARE(odd.size(), 3);
    QCOMPARE(odd.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("study"));
}


QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
