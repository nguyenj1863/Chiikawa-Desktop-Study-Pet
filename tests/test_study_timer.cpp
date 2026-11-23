#include "StudyTimer.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <thread>

class TestStudyTimer : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsInIdle();
    void countdownCompletes();
    void pauseAndResume();
    void resetReturnsToIdle();
    void cancelReturnsToIdle();
    void neverNegative();
    void completionEmitsOnce();
    void formatRemaining();

private:
    StudyTimer* m_timer = nullptr;
};

void TestStudyTimer::init()
{
    m_timer = new StudyTimer();
}

void TestStudyTimer::cleanup()
{
    delete m_timer;
    m_timer = nullptr;
}

void TestStudyTimer::startsInIdle()
{
    QCOMPARE(m_timer->state(), StudyTimer::State::Idle);
    QVERIFY(!m_timer->isActive());
}

void TestStudyTimer::countdownCompletes()
{
    m_timer->configure(std::chrono::milliseconds{400});
    QSignalSpy completed(m_timer, &StudyTimer::completed);
    QSignalSpy stateSpy(m_timer, &StudyTimer::stateChanged);

    m_timer->start();
    QCOMPARE(m_timer->state(), StudyTimer::State::Running);

    QVERIFY(completed.wait(2000));
    QCOMPARE(completed.count(), 1);
    QCOMPARE(m_timer->state(), StudyTimer::State::Completed);
    QCOMPARE(m_timer->remaining().count(), 0);
}

void TestStudyTimer::pauseAndResume()
{
    m_timer->configure(std::chrono::milliseconds{2000});
    m_timer->start();
    QTest::qWait(200);
    m_timer->pause();
    QCOMPARE(m_timer->state(), StudyTimer::State::Paused);
    const auto pausedRem = m_timer->remaining();
    QVERIFY(pausedRem > std::chrono::milliseconds{0});
    QVERIFY(pausedRem < std::chrono::milliseconds{2000});

    QTest::qWait(300);
    QCOMPARE(m_timer->remaining(), pausedRem); // frozen while paused

    m_timer->resume();
    QCOMPARE(m_timer->state(), StudyTimer::State::Running);
    QSignalSpy completed(m_timer, &StudyTimer::completed);
    QVERIFY(completed.wait(3000));
}

void TestStudyTimer::resetReturnsToIdle()
{
    m_timer->configure(std::chrono::seconds{60});
    m_timer->start();
    m_timer->reset();
    QCOMPARE(m_timer->state(), StudyTimer::State::Idle);
    QCOMPARE(m_timer->remaining(), m_timer->configuredDuration());
}

void TestStudyTimer::cancelReturnsToIdle()
{
    m_timer->configure(std::chrono::seconds{30});
    m_timer->start();
    m_timer->pause();
    m_timer->cancel();
    QCOMPARE(m_timer->state(), StudyTimer::State::Idle);
}

void TestStudyTimer::neverNegative()
{
    m_timer->configure(std::chrono::milliseconds{100});
    m_timer->start();
    QSignalSpy completed(m_timer, &StudyTimer::completed);
    QVERIFY(completed.wait(1000));
    QVERIFY(m_timer->remaining() >= std::chrono::milliseconds{0});
    QCOMPARE(m_timer->remainingSeconds() >= 0, true);
}

void TestStudyTimer::completionEmitsOnce()
{
    m_timer->configure(std::chrono::milliseconds{200});
    QSignalSpy completed(m_timer, &StudyTimer::completed);
    m_timer->start();
    QVERIFY(completed.wait(1500));
    QTest::qWait(500);
    QCOMPARE(completed.count(), 1);
}

void TestStudyTimer::formatRemaining()
{
    QCOMPARE(StudyTimer::formatRemaining(std::chrono::milliseconds{0}), QStringLiteral("00:00"));
    QCOMPARE(StudyTimer::formatRemaining(std::chrono::seconds{65}), QStringLiteral("01:05"));
    QCOMPARE(StudyTimer::formatRemaining(std::chrono::seconds{3661}), QStringLiteral("1:01:01"));
}

QTEST_MAIN(TestStudyTimer)
#include "test_study_timer.moc"
