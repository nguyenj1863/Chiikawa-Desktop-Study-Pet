#include "study/evaluation/ChoiceEvaluators.h"
#include "study/progress/ProgressTracker.h"
#include "study/progress/ReviewScheduler.h"
#include "study/practice/SetQuestionProvider.h"
#include "study/StudyDatabase.h"

#include <QRandomGenerator>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace ReviewScheduler;

namespace {
QDateTime at(int y, int m, int d, int hour = 12)
{
    return QDateTime(QDate(y, m, d), QTime(hour, 0), QTimeZone::LocalTime);
}
Card reviewCard(int interval, double ease, const QDateTime& now)
{
    Card c;
    c.state = CardState::Review;
    c.intervalDays = interval;
    c.ease = ease;
    c.dueDay = studyDay(now);
    c.reps = 3;
    return c;
}
} // namespace

class TestReviewScheduler : public QObject {
    Q_OBJECT
private slots:
    void newCardRatings();
    void learningCardGraduates();
    void reviewIntervalsFollowSm2();
    void againLapsesAndLowersEase();
    void easeNeverDropsBelowTheFloor();
    void intervalIsCappedAndAtLeastADay();
    void fuzzStaysWithinFivePercent();
    void studyDayRollsOverAtFourAm();
    void dueDetection();
    void previewLabels();
    void formatsIntervals();
    void ratingFromVerdict();
    void trackerRecordsRatingsAndSchedules();
    void cramDoesNotMoveTheSchedule();
    void practiceOrderPutsDueFirstThenNewThenTheRest();
};

void TestReviewScheduler::newCardRatings()
{
    const QDateTime now = at(2026, 3, 10);
    const Card again = schedule(Card(), Rating::Again, now);
    QCOMPARE(again.state, CardState::Learning);
    QCOMPARE(again.dueAt, now.toMSecsSinceEpoch() + 10 * 60 * 1000);
    QCOMPARE(schedule(Card(), Rating::Hard, now).state, CardState::Learning);

    const Card good = schedule(Card(), Rating::Good, now);
    QCOMPARE(good.state, CardState::Review);
    QCOMPARE(good.intervalDays, 1);
    QCOMPARE(good.dueDay, studyDay(now) + 1);
    QCOMPARE(schedule(Card(), Rating::Easy, now).intervalDays, 4);
    QVERIFY(schedule(Card(), Rating::None, now) == Card());
}

void TestReviewScheduler::learningCardGraduates()
{
    const QDateTime now = at(2026, 3, 10);
    Card learning = schedule(Card(), Rating::Again, now);
    learning = schedule(learning, Rating::Good, now.addSecs(700));
    QCOMPARE(learning.state, CardState::Review);
    QCOMPARE(learning.intervalDays, 1);
}

void TestReviewScheduler::reviewIntervalsFollowSm2()
{
    const QDateTime now = at(2026, 3, 10);
    const Card base = reviewCard(10, 2.5, now);
    const Card hard = schedule(base, Rating::Hard, now);
    const Card good = schedule(base, Rating::Good, now);
    const Card easy = schedule(base, Rating::Easy, now);
    QCOMPARE(hard.intervalDays, 12);
    QCOMPARE(good.intervalDays, 25);
    QCOMPARE(easy.intervalDays, 33);   // 10 * 2.5 * 1.3 = 32.5
    QVERIFY(qFuzzyCompare(hard.ease, 2.35));
    QVERIFY(qFuzzyCompare(good.ease, 2.5));
    QVERIFY(qFuzzyCompare(easy.ease, 2.65));
    QCOMPARE(good.dueDay, studyDay(now) + 25);
    QCOMPARE(good.reps, 4);
    // Every step up is at least a day longer than the one below it, even for tiny intervals.
    const Card small = reviewCard(1, 1.3, now);
    QVERIFY(schedule(small, Rating::Good, now).intervalDays > schedule(small, Rating::Hard, now).intervalDays);
    QVERIFY(schedule(small, Rating::Easy, now).intervalDays > schedule(small, Rating::Good, now).intervalDays);
}

void TestReviewScheduler::againLapsesAndLowersEase()
{
    const QDateTime now = at(2026, 3, 10);
    const Card lapsed = schedule(reviewCard(30, 2.5, now), Rating::Again, now);
    QCOMPARE(lapsed.state, CardState::Learning);
    QCOMPARE(lapsed.lapses, 1);
    QVERIFY(qFuzzyCompare(lapsed.ease, 2.3));
    QCOMPARE(lapsed.intervalDays, 1);
    // Relearning graduates to the post-lapse interval.
    QCOMPARE(schedule(lapsed, Rating::Good, now.addSecs(700)).intervalDays, 1);
}

void TestReviewScheduler::easeNeverDropsBelowTheFloor()
{
    const QDateTime now = at(2026, 3, 10);
    QVERIFY(qFuzzyCompare(schedule(reviewCard(5, 1.35, now), Rating::Again, now).ease, 1.3));
    QVERIFY(qFuzzyCompare(schedule(reviewCard(5, 1.35, now), Rating::Hard, now).ease, 1.3));
}

void TestReviewScheduler::intervalIsCappedAndAtLeastADay()
{
    const QDateTime now = at(2026, 3, 10);
    QCOMPARE(schedule(reviewCard(300, 3.0, now), Rating::Easy, now).intervalDays, 365);
    QVERIFY(schedule(reviewCard(0, 2.5, now), Rating::Good, now).intervalDays >= 1);
}

void TestReviewScheduler::fuzzStaysWithinFivePercent()
{
    const QDateTime now = at(2026, 3, 10);
    QRandomGenerator rng(42);
    const Card base = reviewCard(100, 2.0, now);   // Good = 200 days
    int lo = 1000;
    int hi = 0;
    for (int i = 0; i < 300; ++i) {
        const int d = schedule(base, Rating::Good, now, {}, &rng).intervalDays;
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    QVERIFY(lo >= 190 && hi <= 210);
    QVERIFY(hi > lo);
    // Without an RNG the result is exact.
    QCOMPARE(schedule(base, Rating::Good, now).intervalDays, 200);
}

void TestReviewScheduler::studyDayRollsOverAtFourAm()
{
    QCOMPARE(studyDay(at(2026, 3, 10, 3)), studyDay(at(2026, 3, 9, 20)));
    QCOMPARE(studyDay(at(2026, 3, 10, 4)), studyDay(at(2026, 3, 9, 20)) + 1);
    // Days with a daylight-saving change still count as one day.
    QCOMPARE(studyDay(at(2026, 3, 9, 12)) - studyDay(at(2026, 3, 8, 12)), 1);
    QCOMPARE(studyDay(at(2026, 11, 2, 12)) - studyDay(at(2026, 11, 1, 12)), 1);
    QCOMPARE(studyDay(QDateTime::fromMSecsSinceEpoch(dayStartMs(20000), QTimeZone::LocalTime)), 20000);
}

void TestReviewScheduler::dueDetection()
{
    const QDateTime now = at(2026, 3, 10, 12);
    QVERIFY(!isDue(Card(), now));
    Card review = reviewCard(5, 2.5, now);
    QVERIFY(isDue(review, now));
    review.dueDay = studyDay(now) + 1;
    QVERIFY(!isDue(review, now));
    QVERIFY(isDue(review, now.addDays(1)));
    const Card learning = schedule(Card(), Rating::Again, now);
    QVERIFY(!isDue(learning, now));
    QVERIFY(isDue(learning, now.addSecs(601)));
}

void TestReviewScheduler::previewLabels()
{
    const QDateTime now = at(2026, 3, 10);
    const Preview fresh = preview(Card(), now);
    QCOMPARE(fresh.again, QStringLiteral("10m"));
    QCOMPARE(fresh.hard, QStringLiteral("10m"));
    QCOMPARE(fresh.good, QStringLiteral("1d"));
    QCOMPARE(fresh.easy, QStringLiteral("4d"));
    const Preview mature = preview(reviewCard(10, 2.5, now), now);
    QCOMPARE(mature.again, QStringLiteral("10m"));
    QCOMPARE(mature.hard, QStringLiteral("12d"));
    QCOMPARE(mature.good, QStringLiteral("25d"));
    QCOMPARE(mature.easy, QStringLiteral("1.1mo"));
}

void TestReviewScheduler::formatsIntervals()
{
    QCOMPARE(formatInterval(30 * 1000), QStringLiteral("<1m"));
    QCOMPARE(formatInterval(10 * 60 * 1000), QStringLiteral("10m"));
    QCOMPARE(formatInterval(2LL * 3600 * 1000), QStringLiteral("2h"));
    QCOMPARE(formatInterval(3LL * 86400 * 1000), QStringLiteral("3d"));
    QCOMPARE(formatInterval(45LL * 86400 * 1000), QStringLiteral("1.5mo"));
    QCOMPARE(formatInterval(365LL * 86400 * 1000), QStringLiteral("1.0y"));
}

void TestReviewScheduler::ratingFromVerdict()
{
    Evaluation e;
    e.verdict = Verdict::Correct;
    QCOMPARE(StudyEval::ratingFor(e), Rating::Good);
    e.verdict = Verdict::Partial;
    QCOMPARE(StudyEval::ratingFor(e), Rating::Hard);
    e.verdict = Verdict::Incorrect;
    QCOMPARE(StudyEval::ratingFor(e), Rating::Again);
    e.verdict = Verdict::NeedsReview;
    QCOMPARE(StudyEval::ratingFor(e), Rating::None);
    e.rating = Rating::Easy;   // an explicit choice wins
    QCOMPARE(StudyEval::ratingFor(e), Rating::Easy);
    const Evaluation self = StudyEval::selfAssessment(SelfRating::PartlyGotIt);
    QCOMPARE(StudyEval::ratingFor(self), Rating::Hard);
    QCOMPARE(self.ratingSource, RatingSource::Chosen);
}

namespace {
Question seedQuestion(StudyDatabase& db)
{
    const qint64 set = db.addStudySet(QStringLiteral("Set")).value_or(0);
    QSqlQuery q(db.connection());
    q.exec(QStringLiteral("INSERT INTO documents(id, set_id, title, kind, status, created_at, updated_at)"
                          " VALUES (1, %1, 'Doc', 1, 4, 1, 1)").arg(set));
    q.exec(QStringLiteral("INSERT INTO questions(id, document_id, set_id, type, prompt, concept, difficulty, payload, created_at)"
                          " VALUES ('qa', 1, %1, 1, 'P', 'topic a', 1, '{}', 1)").arg(set));
    Question question;
    question.id = QStringLiteral("qa");
    question.documentId = 1;
    question.setId = set;
    question.topic = QStringLiteral("topic a");
    question.prompt = QStringLiteral("P");
    return question;
}
} // namespace

void TestReviewScheduler::trackerRecordsRatingsAndSchedules()
{
    QTemporaryDir dir;
    StudyDatabase::setStorageDirectory(dir.path());
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    const Question question = seedQuestion(db);
    ProgressTracker tracker(&db);
    QCOMPARE(tracker.card(question.id).state, CardState::New);

    Evaluation good;
    good.verdict = Verdict::Correct;
    good.score = 1.0;
    tracker.record(question, good);
    const Card card = tracker.card(question.id);
    QCOMPARE(card.state, CardState::Review);
    QCOMPARE(card.reps, 1);

    // The concept row is updated in place by the upsert.
    Evaluation bad;
    bad.verdict = Verdict::Incorrect;
    tracker.record(question, bad);
    QSqlQuery q(db.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT seen, correct, incorrect FROM concept_stats")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
    QCOMPARE(q.value(1).toInt(), 1);
    QCOMPARE(q.value(2).toInt(), 1);
    QVERIFY(q.next() == false);
    QVERIFY(q.exec(QStringLiteral("SELECT rating FROM attempts ORDER BY id")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), static_cast<int>(Rating::Good));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), static_cast<int>(Rating::Again));
    q.finish();
    QCOMPARE(tracker.card(question.id).lapses, 1);

    QVERIFY(tracker.reset(0));
    QCOMPARE(tracker.card(question.id).state, CardState::New);
}

void TestReviewScheduler::cramDoesNotMoveTheSchedule()
{
    QTemporaryDir dir;
    StudyDatabase::setStorageDirectory(dir.path());
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    const Question question = seedQuestion(db);
    ProgressTracker tracker(&db);
    Evaluation good;
    good.verdict = Verdict::Correct;
    good.score = 1.0;
    tracker.record(question, good, /*reschedule=*/false);
    QCOMPARE(tracker.card(question.id).state, CardState::New);
    QSqlQuery q(db.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM attempts")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);   // the attempt is still recorded
}

void TestReviewScheduler::practiceOrderPutsDueFirstThenNewThenTheRest()
{
    const QDateTime now = at(2026, 3, 10);
    QVector<Question> questions;
    QHash<QString, Card> cards;
    for (int i = 0; i < 4; ++i) {
        Question due;
        due.id = QStringLiteral("due%1").arg(i);
        Card dueCard = reviewCard(5, 2.5, now);   // due today
        cards.insert(due.id, dueCard);
        Question fresh;
        fresh.id = QStringLiteral("new%1").arg(i);   // no card: new
        Question later;
        later.id = QStringLiteral("later%1").arg(i);
        Card laterCard = reviewCard(5, 2.5, now);
        laterCard.dueDay = studyDay(now) + 3;
        cards.insert(later.id, laterCard);
        questions << later << fresh << due;
    }
    QRandomGenerator rng(7);
    const QVector<Question> ordered = SetQuestionProvider::order(
        questions, [&cards](const QString& id) { return cards.value(id); }, now, rng);
    QCOMPARE(ordered.size(), questions.size());
    for (int i = 0; i < 4; ++i) {
        QVERIFY2(ordered.at(i).id.startsWith(QStringLiteral("due")), qPrintable(ordered.at(i).id));
        QVERIFY2(ordered.at(4 + i).id.startsWith(QStringLiteral("new")), qPrintable(ordered.at(4 + i).id));
        QVERIFY2(ordered.at(8 + i).id.startsWith(QStringLiteral("later")), qPrintable(ordered.at(8 + i).id));
    }
    // Different seeds shuffle within a group but never across groups.
    QRandomGenerator other(99);
    const QVector<Question> again = SetQuestionProvider::order(
        questions, [&cards](const QString& id) { return cards.value(id); }, now, other);
    QVERIFY(again.at(0).id.startsWith(QStringLiteral("due")));
    QVERIFY(again.at(11).id.startsWith(QStringLiteral("later")));
}

QTEST_MAIN(TestReviewScheduler)
#include "test_review_scheduler.moc"
