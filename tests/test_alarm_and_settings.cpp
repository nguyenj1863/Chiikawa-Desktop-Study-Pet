#include "AlarmScheduler.h"
#include "AlarmSoundManager.h"
#include "AppSettings.h"
#include "CollapsibleSection.h"
#include "SettingsDefaults.h"
#include "SettingsPanel.h"
#include "StudyTimer.h"

#include <QComboBox>
#include <QDateTime>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimeEdit>

namespace {

void clearAllSettings()
{
    QSettings settings(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    settings.clear();
    settings.sync();
}

struct FakeClock {
    QDateTime wall;
    qint64 monoMs = 100000;

    AlarmScheduler::WallClockFn wallFn()
    {
        return [this]() { return wall; };
    }
    AlarmScheduler::MonotonicClockFn monoFn()
    {
        return [this]() { return monoMs; };
    }
    void advance(qint64 ms)
    {
        wall = wall.addMSecs(ms);
        monoMs += ms;
    }
};

} // namespace

class TestAlarmAndSettings : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void newAlarmDefaultsToCurrentLocalTimeAndDisabled();
    void enablingAtCurrentMinuteSchedulesTomorrow();
    void savedAlarmPersistsAndRecovers();
    void dailyRecurrenceAndNoDuplicates();
    void sleepResumeWithinGraceTriggersOnce();
    void sleepResumePastGraceAdvancesToTomorrow();
    void systemClockJumpRecalculatesSchedule();
    void snoozeFiveTenFifteenAndDuplicatePrevention();
    void dismissPreservesRecurringAlarm();
    void snoozeRestartRecovery();
    void snoozeOverlappingNextDailyOccurrencePreventsDuplicate();
    void studyTimerAndAlarmRemainIndependent();
    void soundManagerPreferencesAndStop();
    void collapsibleSectionsAndSettingsPanelUi();
};

void TestAlarmAndSettings::init()
{
    clearAllSettings();
}

void TestAlarmAndSettings::cleanup()
{
    clearAllSettings();
}

void TestAlarmAndSettings::newAlarmDefaultsToCurrentLocalTimeAndDisabled()
{
    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;

    QCOMPARE(scheduler.isEnabled(), false);
    QCOMPARE(scheduler.isConfigured(), false);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Inactive);

    // SettingsPanel initializes a brand-new alarm time picker to current local computer time
    // without automatically enabling the alarm
    const QTime before = QDateTime::currentDateTime().time();
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.syncFromAlarm();
    const QTime after = QDateTime::currentDateTime().time();

    auto* timeEdit = panel.findChild<QTimeEdit*>();
    QVERIFY(timeEdit != nullptr);
    const QTime displayed = timeEdit->time();
    QVERIFY(displayed.hour() == before.hour() || displayed.hour() == after.hour());
    QVERIFY(displayed.minute() == before.minute() || displayed.minute() == after.minute());
    QCOMPARE(scheduler.isEnabled(), false);
}

void TestAlarmAndSettings::enablingAtCurrentMinuteSchedulesTomorrow()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(14, 35, 10));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(14, 35, 0), true);

    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 25), QTime(14, 35, 0)));
}

void TestAlarmAndSettings::savedAlarmPersistsAndRecovers()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(10, 0, 0));

    {
        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        scheduler.configureAlarm(QTime(17, 30, 0), true);
    }

    {
        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        scheduler.restoreFromSettings();
        QCOMPARE(scheduler.isEnabled(), true);
        QCOMPARE(scheduler.alarmTime(), QTime(17, 30, 0));
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
        QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 24), QTime(17, 30, 0)));
    }
}

void TestAlarmAndSettings::dailyRecurrenceAndNoDuplicates()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 29, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(17, 30, 0), true);
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 24), QTime(17, 30, 0)));

    // Advance to 17:30:01 and evaluate
    clock.advance(61 * 1000);
    scheduler.checkNow();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);

    // Re-evaluating while ringing must NOT duplicate
    scheduler.checkNow();
    QCOMPARE(spy.count(), 1);

    // Dismissing schedules tomorrow's 17:30
    scheduler.dismiss();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 25), QTime(17, 30, 0)));

    // Re-evaluating at 17:30:05 today must NOT re-trigger
    clock.advance(4 * 1000);
    scheduler.checkNow();
    QCOMPARE(spy.count(), 1);
}

void TestAlarmAndSettings::sleepResumeWithinGraceTriggersOnce()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 20, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(17, 30, 0), true);

    // Simulate Windows sleep: wall clock jumps 15 minutes (to 17:35, 5m after 17:30 -> within 15m grace)
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 35, 0));
    scheduler.checkNow();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);
}

void TestAlarmAndSettings::sleepResumePastGraceAdvancesToTomorrow()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 20, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(17, 30, 0), true);

    // Simulate long sleep: wakes up 2 hours later (past 15m grace window)
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(19, 30, 0));
    scheduler.checkNow();

    QCOMPARE(spy.count(), 0);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 25), QTime(17, 30, 0)));
}

void TestAlarmAndSettings::systemClockJumpRecalculatesSchedule()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(16, 0, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    scheduler.configureAlarm(QTime(17, 30, 0), true);

    // Clock jumps backward by 1 hour
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(15, 0, 0));
    scheduler.checkNow();

    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 24), QTime(17, 30, 0)));
}

void TestAlarmAndSettings::snoozeFiveTenFifteenAndDuplicatePrevention()
{
    const int durations[] = {5, 10, 15};
    for (int mins : durations) {
        clearAllSettings();
        FakeClock clock;
        clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 29, 0));

        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        QSignalSpy triggerSpy(&scheduler, &AlarmScheduler::alarmTriggered);
        QSignalSpy snoozeSpy(&scheduler, &AlarmScheduler::alarmSnoozed);

        scheduler.configureAlarm(QTime(17, 30, 0), true);
        clock.advance(60 * 1000);
        scheduler.checkNow();
        QCOMPARE(triggerSpy.count(), 1);
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);

        // First snooze succeeds
        QVERIFY(scheduler.snooze(mins));
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Snoozed);
        QCOMPARE(snoozeSpy.count(), 1);
        QCOMPARE(scheduler.snoozeTargetTime(), clock.wall.addSecs(mins * 60));

        // Duplicate rapid snooze click must be rejected
        QCOMPARE(scheduler.snooze(mins), false);
        QCOMPARE(snoozeSpy.count(), 1);

        // Advance just before expiry: still snoozed
        clock.advance((mins * 60 - 5) * 1000);
        scheduler.checkNow();
        QCOMPARE(triggerSpy.count(), 1);

        // Advance past expiry: rings again with fromSnooze == true
        clock.advance(6 * 1000);
        scheduler.checkNow();
        QCOMPARE(triggerSpy.count(), 2);
        QCOMPARE(triggerSpy.at(1).at(1).toBool(), true);
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);
    }
}

void TestAlarmAndSettings::dismissPreservesRecurringAlarm()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(8, 0, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    scheduler.configureAlarm(QTime(8, 1, 0), true);

    clock.advance(61 * 1000);
    scheduler.checkNow();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);

    scheduler.snooze(5);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Snoozed);

    // Dismissing while snoozed clears pending snooze and preserves daily recurrence
    scheduler.dismiss();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
    QVERIFY(!scheduler.snoozeTargetTime().isValid());
    QCOMPARE(scheduler.nextTriggerTime(), QDateTime(QDate(2026, 11, 25), QTime(8, 1, 0)));
}

void TestAlarmAndSettings::snoozeRestartRecovery()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 30, 0));

    {
        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        scheduler.configureAlarm(QTime(17, 30, 0), true);
        // Simulate trigger at 17:30 next day
        clock.wall = QDateTime(QDate(2026, 11, 25), QTime(17, 30, 5));
        scheduler.checkNow();
        QVERIFY(scheduler.snooze(5)); // target 17:35:05
    }

    // Restart 2 minutes later (17:32:05) -> pending snooze recovered
    clock.wall = QDateTime(QDate(2026, 11, 25), QTime(17, 32, 5));
    {
        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);
        scheduler.restoreFromSettings();
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Snoozed);
        QCOMPARE(spy.count(), 0);

        clock.advance(3 * 60 * 1000 + 1000);
        scheduler.checkNow();
        QCOMPARE(spy.count(), 1);
    }
}

void TestAlarmAndSettings::snoozeOverlappingNextDailyOccurrencePreventsDuplicate()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 29, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(17, 30, 0), true);
    clock.advance(60 * 1000);
    scheduler.checkNow();
    QCOMPARE(spy.count(), 1);

    QVERIFY(scheduler.snooze(10));
    // Jump all the way to next day's 17:30:00 while snooze was pending
    clock.wall = QDateTime(QDate(2026, 11, 25), QTime(17, 30, 0));
    scheduler.checkNow();

    // Must ring only once (not both snooze and daily)
    QCOMPARE(spy.count(), 2);
}

void TestAlarmAndSettings::studyTimerAndAlarmRemainIndependent()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(14, 0, 0));

    AppSettings settings;
    StudyTimer studyTimer;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());

    scheduler.configureAlarm(QTime(17, 30, 0), true);
    studyTimer.configure(std::chrono::seconds{25 * 60});
    studyTimer.start();

    QCOMPARE(studyTimer.state(), StudyTimer::State::Running);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);

    // Restoring settings defaults must not stop studyTimer or disable alarm
    settings.restoreDefaults();
    QCOMPARE(studyTimer.state(), StudyTimer::State::Running);
    QCOMPARE(scheduler.isEnabled(), true);
    QCOMPARE(scheduler.alarmTime(), QTime(17, 30, 0));

    studyTimer.reset();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Scheduled);
}

void TestAlarmAndSettings::soundManagerPreferencesAndStop()
{
    AlarmSoundManager sound;
    QCOMPARE(sound.selectedSound(), SettingsDefaults::defaultAlarmSoundId());
    QVERIFY(qAbs(sound.volume() - SettingsDefaults::kAlarmVolume) < 0.001);
    QCOMPARE(AlarmSoundManager::availableSounds().size(), 3);

    sound.setVolume(2.0);
    QVERIFY(qAbs(sound.volume() - 1.0) < 0.001);
    sound.setVolume(-0.5);
    QVERIFY(qAbs(sound.volume() - 0.0) < 0.001);

    sound.setSelectedSound(QStringLiteral("tiny_bell"));
    QCOMPARE(sound.selectedSound(), QStringLiteral("tiny_bell"));

    sound.setSelectedSound(QStringLiteral("unknown_id"));
    QCOMPARE(sound.selectedSound(), SettingsDefaults::defaultAlarmSoundId());

    sound.setEnabled(false);
    sound.playAlarmSequence();
    QCOMPARE(sound.isPlaying(), false);

    sound.setEnabled(true);
    sound.setVolume(0.0); // silent for automated test
    sound.preview();
    sound.stop();
    QCOMPARE(sound.isPlaying(), false);
}

void TestAlarmAndSettings::collapsibleSectionsAndSettingsPanelUi()
{
    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;

    CollapsibleSection section(QStringLiteral("Alarm"), false);
    QCOMPARE(section.isExpanded(), false);
    section.setExpanded(true, false);
    QCOMPARE(section.isExpanded(), true);
    section.setExpanded(false, false);
    QCOMPARE(section.isExpanded(), false);

    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.syncFromSettings();
    panel.syncFromTimer();
    panel.syncFromAlarm();

    // Presets & controls exist
    auto* playBtn = panel.findChild<QPushButton*>(QStringLiteral("primary"));
    QVERIFY(playBtn != nullptr);

    // Sound dropdown exists (snooze uses segmented pixel buttons)
    const auto combos = panel.findChildren<QComboBox*>();
    QVERIFY(combos.size() >= 1);

    // Time editor exists for clock alarm
    auto* timeEdit = panel.findChild<QTimeEdit*>();
    QVERIFY(timeEdit != nullptr);
}

QTEST_MAIN(TestAlarmAndSettings)
#include "test_alarm_and_settings.moc"
