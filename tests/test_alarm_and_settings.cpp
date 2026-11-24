#include "AlarmScheduler.h"
#include "AlarmSoundManager.h"
#include "AppSettings.h"
#include "CollapsibleSection.h"
#include "SettingsDefaults.h"
#include "SettingsPanel.h"
#include "StudyTimer.h"

#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

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
    void oneTimeAlarmTriggersOnceAndDeactivatesOnDismiss();
    void sleepResumeWithinGraceTriggersOnce();
    void sleepResumePastGraceExpiresOneTimeAlarm();
    void systemClockJumpRecalculatesSchedule();
    void snoozeFiveTenFifteenAndDuplicatePrevention();
    void dismissAfterSnoozeDeactivatesOneTimeAlarm();
    void snoozeRestartRecovery();
    void legacyDailyAlarmMigrationDoesNotAutoActivate();
    void timeFormatConversions12hAnd24h();
    void occurrenceFormattingTodayAndTomorrow();
    void studyTimerAndAlarmRemainIndependent();
    void soundManagerPreferencesAndStop();
    void collapsibleHeadersAndCompactSettingsLayout();
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

    const QTime before = QDateTime::currentDateTime().time();
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.syncFromAlarm();
    const QTime after = QDateTime::currentDateTime().time();

    const QTime displayed = panel.selectedAlarmTime();
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

void TestAlarmAndSettings::oneTimeAlarmTriggersOnceAndDeactivatesOnDismiss()
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

    // Dismissing marks the one-time alarm Inactive with NO recurring next day
    scheduler.dismiss();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Inactive);
    QCOMPARE(scheduler.isEnabled(), false);
    QVERIFY(!scheduler.nextTriggerTime().isValid());

    // Advancing to tomorrow at 17:30 must NOT ring again
    clock.wall = QDateTime(QDate(2026, 11, 25), QTime(17, 30, 1));
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

    // Simulate Windows sleep: wakes 5m after 17:30 -> within 15m grace
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 35, 0));
    scheduler.checkNow();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);
}

void TestAlarmAndSettings::sleepResumePastGraceExpiresOneTimeAlarm()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 20, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    QSignalSpy spy(&scheduler, &AlarmScheduler::alarmTriggered);

    scheduler.configureAlarm(QTime(17, 30, 0), true);

    // Wakes up 2 hours later (past 15m grace window) -> expires cleanly without recurring
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(19, 30, 0));
    scheduler.checkNow();

    QCOMPARE(spy.count(), 0);
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Inactive);
    QCOMPARE(scheduler.isEnabled(), false);
    QVERIFY(!scheduler.nextTriggerTime().isValid());
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

        QVERIFY(scheduler.snooze(mins));
        QCOMPARE(scheduler.state(), AlarmScheduler::State::Snoozed);
        QCOMPARE(snoozeSpy.count(), 1);
        QCOMPARE(scheduler.snoozeTargetTime(), clock.wall.addSecs(mins * 60));
        QCOMPARE(scheduler.alarmTime(), QTime(17, 30, 0)); // original time preserved

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

void TestAlarmAndSettings::dismissAfterSnoozeDeactivatesOneTimeAlarm()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(8, 0, 0));

    AppSettings settings;
    AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
    scheduler.configureAlarm(QTime(8, 1, 0), true);

    clock.advance(61 * 1000);
    scheduler.checkNow();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Ringing);

    QVERIFY(scheduler.snooze(5));
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Snoozed);

    // Dismissing while snoozed clears pending snooze and marks the one-time alarm Inactive
    scheduler.dismiss();
    QCOMPARE(scheduler.state(), AlarmScheduler::State::Inactive);
    QCOMPARE(scheduler.isEnabled(), false);
    QVERIFY(!scheduler.snoozeTargetTime().isValid());
    QVERIFY(!scheduler.nextTriggerTime().isValid());
}

void TestAlarmAndSettings::snoozeRestartRecovery()
{
    FakeClock clock;
    clock.wall = QDateTime(QDate(2026, 11, 24), QTime(17, 30, 0));

    {
        AppSettings settings;
        AlarmScheduler scheduler(&settings, clock.wallFn(), clock.monoFn());
        scheduler.configureAlarm(QTime(17, 30, 0), true);
        clock.wall = QDateTime(QDate(2026, 11, 25), QTime(17, 30, 5));
        scheduler.checkNow();
        QVERIFY(scheduler.snooze(5));
    }

    // Restart 2 minutes later -> pending snooze recovered
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

void TestAlarmAndSettings::legacyDailyAlarmMigrationDoesNotAutoActivate()
{
    {
        QSettings raw(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
        raw.setValue(QStringLiteral("alarm/configured"), true);
        raw.setValue(QStringLiteral("alarm/enabled"), true);
        raw.setValue(QStringLiteral("alarm/time"), QStringLiteral("07:30"));
        raw.setValue(QStringLiteral("alarm/nextTriggerUtcMs"), QDateTime::currentDateTime().addSecs(3600).toMSecsSinceEpoch());
        raw.remove(QStringLiteral("alarm/oneTimeMode"));
        raw.sync();
    }

    AppSettings settings;
    // Time is preserved, but legacy recurring alarm is safely deactivated on migration
    QCOMPARE(settings.alarmTime(), QTime(7, 30));
    QCOMPARE(settings.alarmEnabled(), false);
    QCOMPARE(settings.alarmNextTriggerUtcMs(), 0);
}

void TestAlarmAndSettings::timeFormatConversions12hAnd24h()
{
    // 5:30 PM <-> 17:30
    QCOMPARE(SettingsPanel::composeAlarmTime(5, 30, true, false), QTime(17, 30));
    {
        const auto p = SettingsPanel::decomposeAlarmTime(QTime(17, 30), false);
        QCOMPARE(p.displayHour, 5);
        QCOMPARE(p.minute, 30);
        QCOMPARE(p.isPm, true);
    }

    // 12:00 AM <-> 00:00
    QCOMPARE(SettingsPanel::composeAlarmTime(12, 0, false, false), QTime(0, 0));
    {
        const auto p = SettingsPanel::decomposeAlarmTime(QTime(0, 0), false);
        QCOMPARE(p.displayHour, 12);
        QCOMPARE(p.minute, 0);
        QCOMPARE(p.isPm, false);
    }

    // 12:00 PM <-> 12:00
    QCOMPARE(SettingsPanel::composeAlarmTime(12, 0, true, false), QTime(12, 0));
    {
        const auto p = SettingsPanel::decomposeAlarmTime(QTime(12, 0), false);
        QCOMPARE(p.displayHour, 12);
        QCOMPARE(p.minute, 0);
        QCOMPARE(p.isPm, true);
    }

    // 11:59 PM <-> 23:59
    QCOMPARE(SettingsPanel::composeAlarmTime(11, 59, true, false), QTime(23, 59));
    {
        const auto p = SettingsPanel::decomposeAlarmTime(QTime(23, 59), false);
        QCOMPARE(p.displayHour, 11);
        QCOMPARE(p.minute, 59);
        QCOMPARE(p.isPm, true);
    }

    // Switching formats in SettingsPanel preserves the exact underlying QTime
    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;
    scheduler.configureAlarm(QTime(17, 30), false);

    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.syncFromAlarm();
    QCOMPARE(panel.selectedAlarmTime(), QTime(17, 30));

    panel.setUse24Hour(true);
    QCOMPARE(panel.use24Hour(), true);
    QCOMPARE(panel.selectedAlarmTime(), QTime(17, 30));
    QCOMPARE(settings.alarmUse24Hour(), true);

    panel.setUse24Hour(false);
    QCOMPARE(panel.use24Hour(), false);
    QCOMPARE(panel.selectedAlarmTime(), QTime(17, 30));
}

void TestAlarmAndSettings::occurrenceFormattingTodayAndTomorrow()
{
    const QDateTime now(QDate(2026, 11, 24), QTime(14, 0, 0));
    const QDateTime todayPm(QDate(2026, 11, 24), QTime(17, 30, 0));
    const QDateTime tomorrowAm(QDate(2026, 11, 25), QTime(8, 0, 0));

    QCOMPARE(AlarmScheduler::formatOccurrence(todayPm, now, false), QStringLiteral("Today at 5:30 PM"));
    QCOMPARE(AlarmScheduler::formatOccurrence(todayPm, now, true), QStringLiteral("Today at 17:30"));
    QCOMPARE(AlarmScheduler::formatOccurrence(tomorrowAm, now, false), QStringLiteral("Tomorrow at 8:00 AM"));
    QCOMPARE(AlarmScheduler::formatOccurrence(tomorrowAm, now, true), QStringLiteral("Tomorrow at 08:00"));
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
    sound.setVolume(0.0);
    sound.preview();
    sound.stop();
    QCOMPARE(sound.isPlaying(), false);
}

void TestAlarmAndSettings::collapsibleHeadersAndCompactSettingsLayout()
{
    CollapsibleSection section(QStringLiteral("Alarm"), false);
    section.setSummary(QStringLiteral("Sound on"));
    section.show();
    QApplication::processEvents();

    // Header height must comfortably fit both title and summary without clipping/overlap
    const int headerH = section.headerWidget()->sizeHint().height();
    const int minNeeded = section.titleLabel()->sizeHint().height() + section.summaryLabel()->sizeHint().height();
    QVERIFY2(headerH >= minNeeded, "Collapsed section header must be tall enough for both title and summary");

    section.setExpanded(true, false);
    QCOMPARE(section.isExpanded(), true);
    QCOMPARE(section.summaryLabel()->isVisible(), false);
    section.setExpanded(false, false);
    QCOMPARE(section.isExpanded(), false);

    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;

    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.prepareToShow();
    QCOMPARE(panel.width(), SettingsPanel::kPanelWidth);
    QVERIFY(panel.width() >= 330 && panel.width() <= 360);

    // Separate hour, minute, AM/PM + sound dropdowns exist
    const auto combos = panel.findChildren<QComboBox*>();
    QVERIFY(combos.size() >= 4);

    // Opening the timer duration editor must NOT widen the popup or overflow the timer card
    auto* countdownBtn = panel.findChild<QPushButton*>(QStringLiteral("countdownDisplay"));
    QVERIFY(countdownBtn != nullptr);
    const int widthBefore = panel.width();
    countdownBtn->click();
    QApplication::processEvents();
    QCOMPARE(panel.width(), widthBefore);

    const auto spins = panel.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 3);
    for (QSpinBox* sb : spins) {
        QVERIFY(sb->isVisibleTo(&panel));
        QVERIFY(sb->geometry().right() <= sb->parentWidget()->width());
    }
}

QTEST_MAIN(TestAlarmAndSettings)
#include "test_alarm_and_settings.moc"
