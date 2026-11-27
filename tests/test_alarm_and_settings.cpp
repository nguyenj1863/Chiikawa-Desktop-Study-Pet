#include "alarm/AlarmScheduler.h"
#include "alarm/AlarmSoundManager.h"
#include "app/AppSettings.h"
#include "ui/CollapsibleSection.h"
#include "timer/CompletionToast.h"
#include "ui/PixelTheme.h"
#include "app/SettingsDefaults.h"
#include "ui/SettingsPanel.h"
#include "timer/StudyTimer.h"
#include "alarm/UISoundManager.h"
#include "app/WindowCoordinator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QPushButton>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QWheelEvent>

namespace {

[[maybe_unused]] const bool kIsolatedSettingsStore = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetTests"));
    return true;
}();

void clearAllSettings()
{
    QSettings settings(AppSettings::storageOrganization(), AppSettings::storageApplication());
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
    void completionToastPositioningHoverTimeoutAndActions();
    void uiSoundManagerThrottlingAndUserOnlyPlayback();
    void redesignedTimerAndAlarmControls();
    void settingsPanelUsesTranslucentGlass();
    void expandedTimerPreservesButtonHeights();
    void windowCoordinatorMovesPetAndSettingsIndependently();
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
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
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

    // Real alarm hour and minute selectors support mouse wheel scrolling
    const auto timeCombos = panel.findChildren<QComboBox*>(QStringLiteral("timeSelect"));
    QVERIFY(timeCombos.size() >= 2);
    QWheelEvent wheelUp(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0), QPoint(0, 120),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(timeCombos[0], &wheelUp); // 5 PM -> 6 PM
    QCoreApplication::sendEvent(timeCombos[1], &wheelUp); // :30 -> :31
    QCOMPARE(panel.selectedAlarmTime(), QTime(18, 31));
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

    // Compact single-line header must fit title and inline summary without vertical clipping or overlap
    const int headerH = section.headerWidget()->sizeHint().height();
    const int minNeeded = std::max(section.titleLabel()->sizeHint().height(),
                                   section.summaryLabel()->sizeHint().height());
    QVERIFY2(headerH >= minNeeded, "Collapsed section header must be tall enough for title and inline summary");
    QVERIFY2(headerH <= 36, "Collapsed section header must stay compact on a single row");
    QVERIFY(!section.titleLabel()->geometry().intersects(section.summaryLabel()->geometry()));

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

void TestAlarmAndSettings::completionToastPositioningHoverTimeoutAndActions()
{
    // 1. Verify bottom-right calculation respects taskbar (1920x1040 work area, 20px margin)
    const QRect workArea(0, 0, 1920, 1040);
    const QSize toastSize(272, 110);
    const QPoint pos = CompletionToast::calculateBottomRightPosition(workArea, toastSize, 20);
    QCOMPARE(pos.x(), 1920 - 272 - 20);
    QCOMPARE(pos.y(), 1040 - 110 - 20);

    // 2. Verify offset multi-monitor work area
    const QRect secondMonitor(1920, 0, 1920, 1040);
    const QPoint pos2 = CompletionToast::calculateBottomRightPosition(secondMonitor, toastSize, 20);
    QCOMPARE(pos2.x(), 1920 + 1920 - 272 - 20);
    QCOMPARE(pos2.y(), 1040 - 110 - 20);

    // 3. Verify 12-second default duration, layout bounds, hover pause/resume, and actions
    CompletionToast toast;
    QCOMPARE(CompletionToast::kDefaultDisplayDurationMs, 12000);

    toast.showNotification(12000);
    QApplication::processEvents();
    QVERIFY(toast.isVisible());
    QVERIFY(toast.remainingDismissMs() > 11000 && toast.remainingDismissMs() <= 12000);

    // Verify buttons are visible, equal height, have pointing-hand cursor, and do not clip
    const auto buttons = toast.findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 2);
    QCOMPARE(buttons[0]->height(), buttons[1]->height());
    for (QPushButton* btn : buttons) {
        QVERIFY(btn->isVisible());
        QCOMPARE(btn->cursor().shape(), Qt::PointingHandCursor);
        QVERIFY(toast.rect().contains(btn->geometry()));
    }

    // Clear any synthetic Enter from offscreen virtual cursor at (0, 0) before measuring countdown
    QEvent initLeave(QEvent::Leave);
    QCoreApplication::sendEvent(&toast, &initLeave);
    QTest::qWait(180);
    QEnterEvent enterEvt(QPointF(10, 10), QPointF(10, 10), QPointF(10, 10));
    QCoreApplication::sendEvent(&toast, &enterEvt);
    QCOMPARE(toast.isAutoHidePaused(), true);
    const int pausedRemaining = toast.remainingDismissMs();
    QVERIFY(pausedRemaining < 12000);

    // While hovered, remaining timeout stays frozen
    QTest::qWait(120);
    QCOMPARE(toast.remainingDismissMs(), pausedRemaining);

    // Leaving resumes from remaining time (does not reset back to 12000)
    QEvent leaveEvt(QEvent::Leave);
    QCoreApplication::sendEvent(&toast, &leaveEvt);
    QCOMPARE(toast.isAutoHidePaused(), false);
    QVERIFY(toast.remainingDismissMs() <= pausedRemaining);

    // Start Again emits signal once and initiates dismissal
    QSignalSpy againSpy(&toast, &CompletionToast::startAgainRequested);
    auto* againBtn = toast.findChild<QPushButton*>(QStringLiteral("primary"));
    QVERIFY(againBtn != nullptr);
    againBtn->click();
    QCOMPARE(againSpy.count(), 1);
}

void TestAlarmAndSettings::uiSoundManagerThrottlingAndUserOnlyPlayback()
{
    UISoundManager uiSound;
    QVERIFY(uiSound.isLoaded());
    QCOMPARE(uiSound.isEnabled(), true);
    QVERIFY(qAbs(uiSound.volume() - UISoundManager::kDefaultVolume) < 0.001);

    // Silent volume for automated test runs while still exercising playback counting & throttling
    uiSound.setVolume(0.0);
    uiSound.resetPlayCount();

    // First click plays; immediate rapid repeat within 45ms is throttled
    uiSound.playClick();
    uiSound.playClick();
    QCOMPARE(uiSound.playCount(), 1);

    // After throttle window passes, next click plays
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    uiSound.playClick();
    QCOMPARE(uiSound.playCount(), 2);

    // Disabled UISoundManager produces zero clicks
    uiSound.setEnabled(false);
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    uiSound.playClick();
    QCOMPARE(uiSound.playCount(), 2);

    // Programmatic UI syncing must NEVER trigger click sounds
    uiSound.setEnabled(true);
    uiSound.resetPlayCount();

    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager alarmSound;

    SettingsPanel panel(&settings, &timer, &scheduler, &alarmSound, &uiSound);
    panel.syncFromSettings();
    panel.syncFromTimer();
    panel.syncFromAlarm();
    panel.setUse24Hour(true);
    QCOMPARE(uiSound.playCount(), 0);

    // User-initiated button click triggers exactly one click sound
    auto* countdownBtn = panel.findChild<QPushButton*>(QStringLiteral("countdownDisplay"));
    QVERIFY(countdownBtn != nullptr);
    countdownBtn->click();
    QCOMPARE(uiSound.playCount(), 1);

    // Disabled control must not produce click sound
    countdownBtn->setEnabled(false);
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    countdownBtn->click();
    QCOMPARE(uiSound.playCount(), 1);

    // Button Sounds checkbox exists and defaults to checked
    auto* btnSoundsCheck = panel.findChild<QCheckBox*>(QStringLiteral("buttonSoundsCheck"));
    QVERIFY(btnSoundsCheck != nullptr);
    QCOMPARE(btnSoundsCheck->isChecked(), true);
}

void TestAlarmAndSettings::settingsPanelUsesTranslucentGlass()
{
    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);

    QVERIFY(panel.testAttribute(Qt::WA_TranslucentBackground));

    // Rounded corners must composite to transparent so the desktop shows through,
    // and the close button uses the minimal floating X style.
    auto* closeBtn = panel.findChild<QPushButton*>(QStringLiteral("closeBtn"));
    QVERIFY(closeBtn != nullptr);
    QVERIFY(closeBtn->width() >= 28 && closeBtn->height() >= 28);

    panel.resize(360, 520);
    panel.ensurePolished();
    const QImage img = panel.grab().toImage();
    QVERIFY(!img.isNull());
    QCOMPARE(img.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(img.pixelColor(img.width() - 1, img.height() - 1).alpha(), 0);
    // Sample a background pixel inside the panel (near top-center margin) to confirm translucency
    const int bgAlpha = img.pixelColor(img.width() / 2, 12).alpha();
    QVERIFY2(bgAlpha > 100 && bgAlpha < 200,
             qPrintable(QStringLiteral("Expected translucent SettingsPanel background alpha, got %1").arg(bgAlpha)));
}
void TestAlarmAndSettings::redesignedTimerAndAlarmControls()
{
    // Shared directional arrow icon assets load cleanly for Alarm dropdowns, Timer steppers, and Onboarding
    const QPixmap downArrow(QStringLiteral(":/assets/icons/arrow_down.png"));
    const QPixmap upArrow(QStringLiteral(":/assets/icons/arrow_up.png"));
    QVERIFY(!downArrow.isNull());
    QVERIFY(!upArrow.isNull());

    // Redesigned pencil icon renders crisp non-null artwork in both enabled and disabled colors
    const QPixmap editEnabled = PixelTheme::icon(PixelIcon::Edit, PixelTheme::kPrimaryText, 16).pixmap(16, 16);
    const QPixmap editDisabled = PixelTheme::icon(PixelIcon::Edit, PixelTheme::kSecondaryText, 16).pixmap(16, 16);
    QVERIFY(!editEnabled.isNull());
    QVERIFY(!editDisabled.isNull());

    // QSS uses shared arrow assets instead of broken CSS border-triangles
    const QString qss = PixelTheme::styleSheet();
    QVERIFY(qss.contains(QStringLiteral("url(:/assets/icons/arrow_down.png)")));
    QVERIFY(qss.contains(QStringLiteral("url(:/assets/icons/arrow_up.png)")));
    QVERIFY(!qss.contains(QStringLiteral("border-top: 5px solid")));

    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.show();
    QTest::qWait(20);

    const int collapsedHeight = panel.height();
    qInfo() << "SettingsPanel dimensions: width=" << panel.width() << "collapsedHeight=" << collapsedHeight;
    QCOMPARE(panel.width(), SettingsPanel::kPanelWidth);
    // 5 sections (Study Timer + collapsed Alarm, Study, Pet Preferences, Quick Actions)
    QVERIFY2(collapsedHeight <= 425, qPrintable(QStringLiteral("Collapsed height too tall: %1").arg(collapsedHeight)));

    // Find Edit pencil button and verify size + state tooltips across Idle, Running, Paused
    QPushButton* editBtn = nullptr;
    for (QPushButton* btn : panel.findChildren<QPushButton*>()) {
        if (btn->accessibleName() == QStringLiteral("Edit custom duration")) {
            editBtn = btn;
            break;
        }
    }
    QVERIFY(editBtn != nullptr);
    QCOMPARE(editBtn->width(), 28);
    QCOMPARE(editBtn->height(), 28);
    QCOMPARE(editBtn->isEnabled(), true);
    QCOMPARE(editBtn->toolTip(), QStringLiteral("Edit custom duration"));

    // Open duration editor and verify Hours/Minutes/Seconds spinboxes + bounds + compact layout
    editBtn->click();
    QTest::qWait(20);
    const auto spins = panel.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 3);
    QCOMPARE(spins[0]->maximum(), 23);
    QCOMPARE(spins[1]->maximum(), 59);
    QCOMPARE(spins[2]->maximum(), 59);
    for (QSpinBox* sb : spins) {
        QVERIFY(sb->isVisible());
        QVERIFY(sb->height() >= 30);
    }

    // Running state keeps edit button visible with disabled state & informative tooltip
    timer.configure(0, 1, 0);
    timer.start();
    panel.syncFromTimer();
    QVERIFY(editBtn->isVisible());
    QCOMPARE(editBtn->isEnabled(), false);
    QVERIFY(editBtn->toolTip().contains(QStringLiteral("Pause")));

    // Paused state keeps edit button visible with disabled state & informative tooltip
    timer.pause();
    panel.syncFromTimer();
    QVERIFY(editBtn->isVisible());
    QCOMPARE(editBtn->isEnabled(), false);
    QVERIFY(editBtn->toolTip().contains(QStringLiteral("Reset")));

    // Reset restores enabled idle state
    timer.reset();
    panel.syncFromTimer();
    QCOMPARE(editBtn->isEnabled(), true);
    QCOMPARE(editBtn->toolTip(), QStringLiteral("Edit custom duration"));

    // Study timer up/down steppers use a translucent white wash over smoked glass (no pastel fill)
    QVERIFY(qss.contains(QStringLiteral("background: rgba(255, 255, 255, 20);")));
    QVERIFY(!qss.contains(QStringLiteral("#F4D5C7"), Qt::CaseInsensitive));

    // Sticky footer keeps Exit Application inside panel bounds (never clipped off-screen)
    QPushButton* exitBtn = nullptr;
    for (QPushButton* btn : panel.findChildren<QPushButton*>()) {
        if (btn->objectName() == QStringLiteral("danger")) {
            exitBtn = btn;
            break;
        }
    }
    QVERIFY(exitBtn != nullptr);
    QVERIFY(exitBtn->isVisible());
    const QPoint exitBottom = exitBtn->mapTo(&panel, QPoint(0, exitBtn->height()));
    QVERIFY(exitBottom.y() <= panel.height());

    // Compact collapsed height sits comfortably beside the pet (and matches pre-edit height after reset)
    QCOMPARE(panel.width(), SettingsPanel::kPanelWidth);
    QCOMPARE(panel.height(), collapsedHeight);

    // Type-to-select on Alarm dropdowns (hour, minute, AM/PM) works both closed and when popup is open
    const auto timeCombos = panel.findChildren<QComboBox*>(QStringLiteral("timeSelect"));
    QCOMPARE(timeCombos.size(), 3);
    QComboBox* hourCombo = timeCombos[0];
    QComboBox* minCombo = timeCombos[1];
    QComboBox* periodCombo = timeCombos[2];

    // Type "1" then "2" in hour dropdown -> selects 12
    QKeyEvent key1(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
    QKeyEvent key2(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(hourCombo, &key1);
    QApplication::sendEvent(hourCombo, &key2);
    QCOMPARE(hourCombo->currentData().toInt(), 12);

    // Type "4" then "5" in minute dropdown -> selects 45
    QKeyEvent key4(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier, QStringLiteral("4"));
    QKeyEvent key5(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(minCombo, &key4);
    QApplication::sendEvent(minCombo, &key5);
    QCOMPARE(minCombo->currentData().toInt(), 45);

    // Type "p" then "a" in AM/PM dropdown -> selects PM then AM
    QKeyEvent keyP(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"));
    QKeyEvent keyA(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QApplication::sendEvent(periodCombo, &keyP);
    QCOMPARE(periodCombo->currentText(), QStringLiteral("PM"));
    QApplication::sendEvent(periodCombo, &keyA);
    QCOMPARE(periodCombo->currentText(), QStringLiteral("AM"));
}

void TestAlarmAndSettings::expandedTimerPreservesButtonHeights()
{
    AppSettings settings;
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    panel.show();
    QApplication::processEvents();

    // Expand every collapsible section AND open the custom duration editor
    for (auto* sec : panel.findChildren<CollapsibleSection*>()) {
        sec->setExpanded(true, false);
    }
    auto* editBtn = panel.findChild<QPushButton*>(QStringLiteral("iconBtn"));
    QVERIFY(editBtn != nullptr);
    QTest::mouseClick(editBtn, Qt::LeftButton);
    QApplication::processEvents();

    auto* playBtn = panel.findChild<QPushButton*>(QStringLiteral("iconBtnPrimary"));
    QVERIFY(playBtn != nullptr);
    QCOMPARE(playBtn->height(), 30);
    QCOMPARE(playBtn->width(), 30);

    // Every icon button inside timerCard must keep its full fixed height (never squished)
    auto* timerCard = panel.findChild<QFrame*>(QStringLiteral("timerCard"));
    QVERIFY(timerCard != nullptr);
    for (auto* btn : timerCard->findChildren<QPushButton*>()) {
        if (btn->objectName() == QLatin1String("iconBtn")
            || btn->objectName() == QLatin1String("iconBtnPrimary")) {
            QVERIFY2(btn->height() >= 28,
                     qPrintable(QStringLiteral("Timer icon button squished to %1px").arg(btn->height())));
        }
    }
}

void TestAlarmAndSettings::windowCoordinatorMovesPetAndSettingsIndependently()
{
    const QRect avail = WindowCoordinator::availableGeometryFor(QGuiApplication::primaryScreen()->geometry().center());

    QWidget pet(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    pet.setFixedSize(120, 120);
    pet.move(avail.topLeft() + QPoint(120, 120));
    pet.show();

    QWidget settings(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    settings.setFixedSize(340, 320);
    settings.show();

    WindowCoordinator coordinator(&pet, &settings);
    coordinator.attachSettings();
    const QPoint attachedPos = settings.pos();
    QCOMPARE(attachedPos.y(), pet.y());
    QVERIFY(attachedPos.x() >= pet.x() + pet.width());

    // Dragging the pet never moves Settings
    QSignalSpy petMovedSpy(&coordinator, &WindowCoordinator::petMoved);
    coordinator.movePetTo(pet.pos() + QPoint(60, 40));
    QCOMPARE(petMovedSpy.count(), 1);
    QCOMPARE(settings.pos(), attachedPos);

    // Dragging Settings never moves the pet
    const QPoint petPos = pet.pos();
    coordinator.moveSettingsTo(attachedPos + QPoint(80, 30));
    QCOMPARE(settings.pos(), attachedPos + QPoint(80, 30));
    QCOMPARE(pet.pos(), petPos);

    // Settings stays within the available monitor bounds
    coordinator.moveSettingsTo(avail.bottomRight() + QPoint(500, 500));
    QVERIFY(avail.contains(settings.frameGeometry().topLeft()));
    QCOMPARE(pet.pos(), petPos);

    // Reopening re-attaches Settings beside the pet's current location
    coordinator.attachSettings();
    QCOMPARE(settings.pos().y(), pet.y());
    QCOMPARE(settings.pos().x(), pet.x() + pet.width() + WindowCoordinator::kGap);
}

QTEST_MAIN(TestAlarmAndSettings)
#include "test_alarm_and_settings.moc"
