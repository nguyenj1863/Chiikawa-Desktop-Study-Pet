#include "MockLlmClient.h"

#include "study/generation/QuestionGenerator.h"
#include "study/generation/QuestionValidator.h"
#include "study/StudyDatabase.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kJoinsPassage = QStringLiteral(
    "A LEFT JOIN returns all rows from the left table, plus the matching rows from the right table. "
    "When there is no match, the result contains NULL values for the columns of the right table. "
    "An INNER JOIN returns only the rows that have matching values in both tables.");

const QString kAutomataPassage = QStringLiteral(
    "A nondeterministic finite automaton (NFA) may have several transitions for the same input symbol. "
    "An NFA may also use epsilon transitions that consume no input. "
    "Every NFA can be converted into an equivalent DFA, so NFAs recognize exactly the regular languages.");

QString singleChoiceJson(const QString& topic = QStringLiteral("LEFT JOIN result"),
                         const QString& evidence = QStringLiteral("A LEFT JOIN returns all rows from the left table, plus the matching rows from the right table."),
                         int correctCount = 1)
{
    QJsonArray options;
    const QStringList texts = {QStringLiteral("All rows from the left table plus matching right-table rows"),
                               QStringLiteral("Only rows that match in both tables"),
                               QStringLiteral("All rows from both tables"),
                               QStringLiteral("Only unmatched rows from the right table")};
    for (int i = 0; i < 4; ++i) {
        options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)},
                                   {QStringLiteral("correct"), i < correctCount},
                                   {QStringLiteral("why"), QStringLiteral("Because the passage says so.")}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("concept"), topic},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("question"), QStringLiteral("What does a LEFT JOIN return?")},
        {QStringLiteral("options"), options},
        {QStringLiteral("explanation"), QStringLiteral("A LEFT JOIN keeps every left-table row.")},
        {QStringLiteral("difficulty"), QStringLiteral("easy")},
    }).toJson(QJsonDocument::Compact));
}

QString multiSelectJson(bool supported = true)
{
    QJsonArray options;
    const QList<QPair<QString, bool>> statements = {
        {QStringLiteral("It may have several transitions for the same input symbol."), true},
        {QStringLiteral("It recognizes more languages than any DFA."), false},
        {QStringLiteral("It recognizes exactly the regular languages."), true},
        {QStringLiteral("It may use epsilon transitions."), true},
    };
    for (const auto& s : statements) {
        options.append(QJsonObject{{QStringLiteral("text"), s.first},
                                   {QStringLiteral("correct"), s.second},
                                   {QStringLiteral("why"), s.second ? QStringLiteral("Stated in the passage.")
                                                                    : QStringLiteral("Every NFA converts to a DFA.")}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("supported"), supported},
        {QStringLiteral("concept"), QStringLiteral("NFA properties")},
        {QStringLiteral("evidence"), QStringLiteral("An NFA may also use epsilon transitions that consume no input.")},
        {QStringLiteral("question"), QStringLiteral("Which statements about an NFA are true?")},
        {QStringLiteral("options"), options},
        {QStringLiteral("explanation"), QStringLiteral("NFAs and DFAs are equally powerful.")},
        {QStringLiteral("difficulty"), QStringLiteral("medium")},
    }).toJson(QJsonDocument::Compact));
}

QString writtenJson(const QString& topic = QStringLiteral("INNER vs LEFT JOIN"))
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("concept"), topic},
        {QStringLiteral("evidence"), QStringLiteral("An INNER JOIN returns only the rows that have matching values in both tables.")},
        {QStringLiteral("question"), QStringLiteral("Explain the difference between INNER JOIN and LEFT JOIN.")},
        {QStringLiteral("expected_answer"), QStringLiteral("An INNER JOIN keeps only matching rows; a LEFT JOIN keeps all left rows and fills NULLs.")},
        {QStringLiteral("key_points"), QJsonArray{QStringLiteral("INNER JOIN keeps only matches"),
                                                  QStringLiteral("LEFT JOIN keeps all left rows")}},
        {QStringLiteral("aliases"), QJsonArray{}},
        {QStringLiteral("explanation"), QStringLiteral("The joins differ in how unmatched rows are handled.")},
        {QStringLiteral("difficulty"), QStringLiteral("hard")},
    }).toJson(QJsonDocument::Compact));
}

Question parsed(QuestionType type, const QString& json)
{
    QString error;
    const auto q = QuestionGenerator::parseResponse(type, json, &error);
    if (!q) {
        qWarning() << "parse failed:" << error;
        return Question{};
    }
    return *q;
}

} // namespace

class TestStudyGeneration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void schemasAreOrderedJson();
    void requestGroundsPromptInPassage();
    void parsesRepliesAndRejectsMalformedOnes();
    void evidenceMustComeFromSource();
    void validatesSingleChoiceRules();
    void validatesMultiSelectRules();
    void validatesWrittenAndDuplicates();
    void stableIdsAndChunkPicking();
    void questionsRoundTripThroughDatabase();

    void generatesGroundedQuestionsForEachFormat();
    void multiSelectFallsBackWhenUnsupported();
    void retriesThenRejectsUngroundedReplies();
    void stopsWhenModelUnavailable();
    void cancelsAndRegenerates();

private:
    struct Fixture {
        qint64 studySet = 0;
        qint64 joins = 0;
        qint64 automata = 0;
    };
    Fixture seed(StudyDatabase& db);
    QTemporaryDir m_storage;
};

void TestStudyGeneration::initTestCase()
{
    QVERIFY(m_storage.isValid());
    StudyDatabase::setStorageDirectory(m_storage.path());
}

void TestStudyGeneration::cleanup()
{
    QDir(m_storage.path()).removeRecursively();
}

TestStudyGeneration::Fixture TestStudyGeneration::seed(StudyDatabase& db)
{
    Fixture f;
    f.studySet = *db.addStudySet(QStringLiteral("Databases"));
    f.joins = *db.addDocument(f.studySet, QStringLiteral("Lecture 4 Joins"), QStringLiteral("C:/l4.pptx"), DocumentKind::Pptx);
    f.automata = *db.addDocument(f.studySet, QStringLiteral("Automata"), QStringLiteral("C:/a.pdf"), DocumentKind::Pdf);
    db.replaceChunks(f.joins, {{0, 0, 0, SegmentKind::Slide, 4, 0, kJoinsPassage},
                               {0, 0, 0, SegmentKind::Slide, 5, 1, kJoinsPassage + QStringLiteral(" Joins combine tables.")}});
    db.replaceChunks(f.automata, {{0, 0, 0, SegmentKind::Page, 2, 0, kAutomataPassage}});
    db.setDocumentStatus(f.joins, DocumentStatus::Extracted);
    db.setDocumentStatus(f.automata, DocumentStatus::Extracted);
    return f;
}

void TestStudyGeneration::schemasAreOrderedJson()
{
    for (const QuestionType t : {QuestionType::SingleChoice, QuestionType::MultiSelect, QuestionType::Written}) {
        const QByteArray schema = QuestionGenerator::schemaFor(t);
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(schema, &err);
        QVERIFY2(doc.isObject(), qPrintable(err.errorString()));
        QCOMPARE(doc.object().value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        // The model quotes its evidence before writing the question.
        QVERIFY(schema.indexOf("\"concept\"") < schema.indexOf("\"evidence\""));
        QVERIFY(schema.indexOf("\"evidence\"") < schema.indexOf("\"question\""));
        const QJsonArray required = doc.object().value(QStringLiteral("required")).toArray();
        QVERIFY(required.contains(QStringLiteral("evidence")));
    }
    QVERIFY(QuestionGenerator::schemaFor(QuestionType::MultiSelect).startsWith("{\"type\":\"object\",\"properties\":{\"supported\""));
    QVERIFY(QuestionGenerator::schemaFor(QuestionType::SingleChoice).contains("\"minItems\":4"));
}

void TestStudyGeneration::requestGroundsPromptInPassage()
{
    const Chunk chunk{7, 3, 1, SegmentKind::Slide, 4, 0, kJoinsPassage};
    const LlmRequest r = QuestionGenerator::buildRequest(QuestionType::SingleChoice, chunk, QStringLiteral("Lecture 4"),
                                                         {QStringLiteral("primary keys")});
    QVERIFY(r.system.contains(QStringLiteral("ONLY facts stated in the passage")));
    QVERIFY(r.system.contains(QStringLiteral("never list the answer options inside the question")));
    QVERIFY(r.system.contains(QStringLiteral("do not describe the question itself")));
    QVERIFY(r.prompt.contains(kJoinsPassage));
    QVERIFY(r.prompt.contains(QStringLiteral("\"Lecture 4\", slide 4")));
    QVERIFY(r.prompt.contains(QStringLiteral("Do not test these concepts again: primary keys")));
    QVERIFY(r.prompt.contains(QStringLiteral("Exactly 4 options")));
    QCOMPARE(r.schema, QuestionGenerator::schemaFor(QuestionType::SingleChoice));
    QVERIFY(r.timeoutMs >= 60000);

    const LlmRequest multi = QuestionGenerator::buildRequest(QuestionType::MultiSelect, chunk, QStringLiteral("L"), {});
    QVERIFY(multi.prompt.contains(QStringLiteral("set \"supported\" to false")));
    QVERIFY(!multi.prompt.contains(QStringLiteral("Do not test")));
}

void TestStudyGeneration::parsesRepliesAndRejectsMalformedOnes()
{
    const Question single = parsed(QuestionType::SingleChoice, QStringLiteral("```json\n") + singleChoiceJson() + QStringLiteral("\n```"));
    QCOMPARE(single.type, QuestionType::SingleChoice);
    QCOMPARE(single.options.size(), 4);
    QCOMPARE(single.options.at(0).id, QStringLiteral("a"));
    QVERIFY(single.options.at(0).correct);
    QCOMPARE(single.difficulty, Difficulty::Easy);
    QCOMPARE(single.sources.size(), 1);
    QVERIFY(single.sources.first().excerpt.startsWith(QStringLiteral("A LEFT JOIN")));

    const Question written = parsed(QuestionType::Written, writtenJson());
    QCOMPARE(written.keyConcepts.size(), 2);
    QCOMPARE(written.difficulty, Difficulty::Hard);
    QCOMPARE(written.answers.size(), 1);
    QCOMPARE(written.answers.first().canonical, written.expectedAnswer);

    // Options repeated inside the question stem are cut off (seen with small local models).
    QString embedded = multiSelectJson();
    embedded.replace(QStringLiteral("Which statements about an NFA are true?"),
                     QStringLiteral(R"(Which statements about an NFA are true?\n1. It may have several transitions\n2. It may use epsilon)"));
    QCOMPARE(parsed(QuestionType::MultiSelect, embedded).prompt, QStringLiteral("Which statements about an NFA are true?"));
    QString listless = singleChoiceJson();
    listless.replace(QStringLiteral("What does a LEFT JOIN return?"), QStringLiteral(R"(What does a LEFT JOIN return?\nThink carefully.)"));
    QVERIFY(parsed(QuestionType::SingleChoice, listless).prompt.contains(QStringLiteral("Think carefully.")));
    // References to "the passage" are removed from the stem the student sees.
    QString referencing = singleChoiceJson();
    referencing.replace(QStringLiteral("What does a LEFT JOIN return?"),
                        QStringLiteral("What does a LEFT JOIN return, according to the provided text?"));
    QCOMPARE(parsed(QuestionType::SingleChoice, referencing).prompt, QStringLiteral("What does a LEFT JOIN return?"));
    referencing = multiSelectJson();
    referencing.replace(QStringLiteral("Which statements about an NFA are true?"),
                        QStringLiteral("Which statements about an NFA are true based on the passage?"));
    QCOMPARE(parsed(QuestionType::MultiSelect, referencing).prompt, QStringLiteral("Which statements about an NFA are true?"));

    QString error;
    bool unsupported = false;
    QVERIFY(!QuestionGenerator::parseResponse(QuestionType::MultiSelect, multiSelectJson(false), &error, &unsupported));
    QVERIFY(unsupported);
    QVERIFY(!QuestionGenerator::parseResponse(QuestionType::SingleChoice, QStringLiteral("I cannot help"), &error));
    QVERIFY(error.contains(QStringLiteral("not JSON")));
    QVERIFY(!QuestionGenerator::parseResponse(QuestionType::SingleChoice, QStringLiteral("{\"question\": "), &error));
    QString badDifficulty = singleChoiceJson();
    badDifficulty.replace(QStringLiteral("\"easy\""), QStringLiteral("\"trivial\""));
    QVERIFY(!QuestionGenerator::parseResponse(QuestionType::SingleChoice, badDifficulty, &error));
}

void TestStudyGeneration::evidenceMustComeFromSource()
{
    using QuestionValidator::evidenceSupported;
    QVERIFY(evidenceSupported(QStringLiteral("returns only the rows that have matching values"), kJoinsPassage));
    // Quote and case differences do not matter.
    QVERIFY(evidenceSupported(QStringLiteral("“A left join RETURNS all rows from the left table”"), kJoinsPassage));
    // Minor drift in a long quote is tolerated...
    QVERIFY(evidenceSupported(QStringLiteral("When there is no match, the result contains NULL values for the columns of the right table, as shown."),
                              kJoinsPassage));
    // ...but fabricated or outside-knowledge evidence is not.
    QVERIFY(!evidenceSupported(QStringLiteral("A FULL OUTER JOIN returns all rows when there is a match in either table."), kJoinsPassage));
    QVERIFY(!evidenceSupported(QStringLiteral("LEFT JOIN"), kJoinsPassage));   // too short to prove anything
    QVERIFY(!evidenceSupported(QStringLiteral("rows from the left table"), QString()));
}

void TestStudyGeneration::validatesSingleChoiceRules()
{
    Question q = parsed(QuestionType::SingleChoice, singleChoiceJson());
    QVERIFY2(QuestionValidator::validate(q, kJoinsPassage).ok, qPrintable(QuestionValidator::validate(q, kJoinsPassage).reason));

    Question twoCorrect = parsed(QuestionType::SingleChoice, singleChoiceJson(QStringLiteral("x"), QStringLiteral("A LEFT JOIN returns all rows from the left table"), 2));
    QVERIFY(QuestionValidator::validate(twoCorrect, kJoinsPassage).reason.contains(QStringLiteral("exactly one")));

    Question three = q;
    three.options.removeLast();
    QVERIFY(QuestionValidator::validate(three, kJoinsPassage).reason.contains(QStringLiteral("4 options")));

    Question dup = q;
    dup.options[2].text = dup.options[1].text.toUpper();
    QVERIFY(QuestionValidator::validate(dup, kJoinsPassage).reason.contains(QStringLiteral("duplicate")));

    Question catchAll = q;
    catchAll.options[3].text = QStringLiteral("All of the above");
    QVERIFY(QuestionValidator::validate(catchAll, kJoinsPassage).reason.contains(QStringLiteral("catch-all")));

    Question giveaway = q;
    giveaway.options[0].text = QStringLiteral("Every single row that exists in the left table together with any and all rows of the right "
                                              "table whose join columns match, padded with NULL values otherwise");
    giveaway.options[1].text = QStringLiteral("Matches");
    giveaway.options[2].text = QStringLiteral("Everything");
    giveaway.options[3].text = QStringLiteral("Nothing");
    QVERIFY(QuestionValidator::validate(giveaway, kJoinsPassage).reason.contains(QStringLiteral("longer")));

    Question ungrounded = q;
    ungrounded.sources[0].excerpt = QStringLiteral("SQL was invented at IBM in the 1970s by researchers.");
    QVERIFY(QuestionValidator::validate(ungrounded, kJoinsPassage).reason.contains(QStringLiteral("not quoted")));

    Question noExplanation = q;
    noExplanation.explanation.clear();
    QVERIFY(!QuestionValidator::validate(noExplanation, kJoinsPassage).ok);
}

void TestStudyGeneration::validatesMultiSelectRules()
{
    Question q = parsed(QuestionType::MultiSelect, multiSelectJson());
    QVERIFY2(QuestionValidator::validate(q, kAutomataPassage).ok, qPrintable(QuestionValidator::validate(q, kAutomataPassage).reason));

    Question oneCorrect = q;
    oneCorrect.options[0].correct = false;
    oneCorrect.options[2].correct = false;
    QVERIFY(QuestionValidator::validate(oneCorrect, kAutomataPassage).reason.contains(QStringLiteral("two or three")));

    Question allCorrect = q;
    for (QuestionOption& o : allCorrect.options) {
        o.correct = true;
    }
    QVERIFY(!QuestionValidator::validate(allCorrect, kAutomataPassage).ok);

    Question unexplained = q;
    unexplained.options[1].explanation.clear();
    QVERIFY(QuestionValidator::validate(unexplained, kAutomataPassage).reason.contains(QStringLiteral("explanations")));
}

void TestStudyGeneration::validatesWrittenAndDuplicates()
{
    Question q = parsed(QuestionType::Written, writtenJson());
    QVERIFY(QuestionValidator::validate(q, kJoinsPassage).ok);

    Question noPoints = q;
    noPoints.keyConcepts.clear();
    QVERIFY(!QuestionValidator::validate(noPoints, kJoinsPassage).ok);
    Question noAnswer = q;
    noAnswer.expectedAnswer.clear();
    QVERIFY(!QuestionValidator::validate(noAnswer, kJoinsPassage).ok);

    QVERIFY(QuestionValidator::validate(q, kJoinsPassage, {QStringLiteral("inner VS left join")}).reason.contains(QStringLiteral("concept")));
    QVERIFY(QuestionValidator::validate(q, kJoinsPassage, {}, {q.prompt.toLower()}).reason.contains(QStringLiteral("duplicate")));

}

void TestStudyGeneration::stableIdsAndChunkPicking()
{
    const QString a = QuestionGenerator::stableId(3, QuestionType::Written, QStringLiteral("What is a join?"));
    QCOMPARE(a, QuestionGenerator::stableId(3, QuestionType::Written, QStringLiteral("  what is a JOIN ")));
    QVERIFY(a != QuestionGenerator::stableId(4, QuestionType::Written, QStringLiteral("What is a join?")));
    QVERIFY(a != QuestionGenerator::stableId(3, QuestionType::SingleChoice, QStringLiteral("What is a join?")));
    QVERIFY(a.startsWith(QStringLiteral("d3-")));

    QVector<Chunk> chunks;
    for (int i = 0; i < 30; ++i) {
        chunks.append({i + 1, 1, 1, SegmentKind::Page, i + 1, i, QString(200, QLatin1Char('x'))});
    }
    chunks[5].text = QStringLiteral("short");
    const QVector<Chunk> picks = QuestionGenerator::pickChunks(chunks, {1, 2}, 6);
    QCOMPARE(picks.size(), 6);
    QVERIFY(picks.first().id >= 3);
    QVERIFY(picks.last().refNumber >= 24);   // spread to the end of the document
    for (const Chunk& c : picks) {
        QVERIFY(c.id != 6);
    }
    QCOMPARE(QuestionGenerator::pickChunks(chunks.mid(0, 3), {}, 10).size(), 3);
    QVector<Chunk> onlyShort = {{1, 1, 1, SegmentKind::Slide, 1, 0, QStringLiteral("tiny")}};
    QCOMPARE(QuestionGenerator::pickChunks(onlyShort, {}, 5).size(), 1);
}

void TestStudyGeneration::questionsRoundTripThroughDatabase()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    Question q = parsed(QuestionType::MultiSelect, multiSelectJson());
    q.id = QuestionGenerator::stableId(f.automata, q.type, q.prompt);
    q.setId = f.studySet;
    q.documentId = f.automata;
    q.sources[0].location = QStringLiteral("p. 2");
    const qint64 chunkId = db.chunks(f.automata).first().id;
    QVERIFY2(db.saveQuestion(q, chunkId, QStringLiteral("qwen3.5:4b-q4_K_M")), qPrintable(db.lastError()));

    const auto loaded = db.question(q.id);
    QVERIFY(loaded);
    QCOMPARE(loaded->type, QuestionType::MultiSelect);
    QCOMPARE(loaded->prompt, q.prompt);
    QCOMPARE(loaded->topic, QStringLiteral("NFA properties"));
    QCOMPARE(loaded->difficulty, Difficulty::Medium);
    QCOMPARE(loaded->options.size(), 4);
    QCOMPARE(loaded->options.at(1).correct, false);
    QCOMPARE(loaded->options.at(1).explanation, QStringLiteral("Every NFA converts to a DFA."));
    QCOMPARE(loaded->category, QStringLiteral("Databases"));
    QCOMPARE(loaded->sources.first().title, QStringLiteral("Automata"));
    QCOMPARE(loaded->sources.first().location, QStringLiteral("p. 2"));
    QCOMPARE(loaded->sources.first().chunkId, chunkId);
    QVERIFY(loaded->isValid());

    QCOMPARE(db.setQuestions(f.studySet).size(), 1);
    QCOMPARE(db.setQuestions(f.studySet, {QuestionType::Written}).size(), 0);
    QCOMPARE(db.usedChunkIds(f.automata), QSet<qint64>{chunkId});
    QCOMPARE(db.document(f.automata)->questionCount, 1);
    db.setDocumentSelected(f.automata, false);
    QCOMPARE(db.setQuestions(f.studySet).size(), 0);
    QCOMPARE(db.setQuestions(f.studySet, {}, false).size(), 1);
    QVERIFY(db.removeQuestions(f.automata));
    QVERIFY(db.documentQuestions(f.automata).isEmpty());
}

void TestStudyGeneration::generatesGroundedQuestionsForEachFormat()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    MockLlmClient llm;
    llm.responder = [](const LlmRequest& r) -> MockLlmClient::Reply {
        const QString kind = MockLlmClient::kindOf(r);
        if (kind == QStringLiteral("multi")) {
            return multiSelectJson();
        }
        if (kind == QStringLiteral("written")) {
            return writtenJson();
        }
        // Quote evidence from whichever passage the request is about.
        if (r.prompt.contains(QStringLiteral("nondeterministic"))) {
            return singleChoiceJson(QStringLiteral("NFA transitions"),
                                    QStringLiteral("An NFA may also use epsilon transitions that consume no input."));
        }
        return singleChoiceJson();
    };
    QuestionGenerator gen(&db, &llm);
    gen.setModelName(QStringLiteral("test-model"));
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QSignalSpy docDone(&gen, &QuestionGenerator::documentFinished);
    QSignalSpy progress(&gen, &QuestionGenerator::progress);

    QVERIFY(gen.start({f.joins, f.automata}, {QuestionType::SingleChoice, QuestionType::Written, QuestionType::MultiSelect}, false));
    QVERIFY(gen.isRunning());
    QCOMPARE(db.document(f.joins)->status, DocumentStatus::Generating);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!gen.isRunning());
    QCOMPARE(finished.first().at(2).toString(), QString());
    QCOMPARE(docDone.count(), 2);
    QVERIFY(progress.count() >= 3);
    QCOMPARE(llm.releaseCount, 1);   // model unloaded after the batch

    // Joins: two chunks rotate through single choice then written. Automata: one chunk, single choice.
    const QVector<Question> joins = db.documentQuestions(f.joins);
    QCOMPARE(joins.size(), 2);
    QCOMPARE(joins.at(0).type, QuestionType::SingleChoice);
    QCOMPARE(joins.at(1).type, QuestionType::Written);
    for (const Question& q : joins) {
        QCOMPARE(q.sources.first().title, QStringLiteral("Lecture 4 Joins"));
        QVERIFY(q.sources.first().location.startsWith(QStringLiteral("slide ")));
        QVERIFY(!q.sources.first().excerpt.isEmpty());
    }
    QCOMPARE(db.document(f.joins)->status, DocumentStatus::Ready);
    QCOMPARE(db.document(f.automata)->status, DocumentStatus::Ready);
    QCOMPARE(finished.first().at(0).toInt(), 3);

    // Generating again only uses chunks without questions; here none are left.
    QVERIFY(!gen.start({f.joins}, {QuestionType::SingleChoice}, false));
}

void TestStudyGeneration::multiSelectFallsBackWhenUnsupported()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    MockLlmClient llm;
    llm.responder = [](const LlmRequest& r) -> MockLlmClient::Reply {
        if (MockLlmClient::kindOf(r) == QStringLiteral("multi")) {
            return multiSelectJson(false);
        }
        return writtenJson(QStringLiteral("NFA vs DFA"));
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.automata}, {QuestionType::MultiSelect, QuestionType::Written}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    // One multi-select request, then the written fallback (whose canned reply quotes the joins
    // passage, so validation rejects it and it is retried once).
    QCOMPARE(llm.requests.size(), 3);
    QCOMPARE(MockLlmClient::kindOf(llm.requests.at(0)), QStringLiteral("multi"));
    QCOMPARE(MockLlmClient::kindOf(llm.requests.at(1)), QStringLiteral("written"));
    QCOMPARE(MockLlmClient::kindOf(llm.requests.at(2)), QStringLiteral("written"));
    // No multi-select question was invented for a passage that cannot support one.
    QVERIFY(db.setQuestions(f.studySet, {QuestionType::MultiSelect}, false).isEmpty());

    // With only multi-select enabled there is no fallback: the chunk is simply skipped.
    MockLlmClient onlyMulti;
    onlyMulti.responder = [](const LlmRequest&) -> MockLlmClient::Reply { return multiSelectJson(false); };
    QuestionGenerator gen2(&db, &onlyMulti);
    QSignalSpy finished2(&gen2, &QuestionGenerator::finished);
    QVERIFY(gen2.start({f.automata}, {QuestionType::MultiSelect}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished2.count(), 1, 5000);
    QCOMPARE(onlyMulti.requests.size(), 1);
    QCOMPARE(finished2.first().at(1).toInt(), 1);   // rejected
    QCOMPARE(db.document(f.automata)->status, DocumentStatus::Extracted);
}

void TestStudyGeneration::retriesThenRejectsUngroundedReplies()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    MockLlmClient llm;
    int calls = 0;
    llm.responder = [&calls](const LlmRequest&) -> MockLlmClient::Reply {
        ++calls;
        if (calls == 1) {
            return QStringLiteral("not json at all");
        }
        if (calls == 2) {
            return LlmError{LlmErrorKind::Timeout, QStringLiteral("slow")};
        }
        return singleChoiceJson(QStringLiteral("made up"), QStringLiteral("SQL was standardized by ANSI in 1986 as a language."));
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.joins}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    // Two chunks x two attempts each.
    QCOMPARE(calls, 4);
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(finished.first().at(1).toInt(), 2);
    QVERIFY(db.documentQuestions(f.joins).isEmpty());
    const auto doc = db.document(f.joins);
    QCOMPARE(doc->status, DocumentStatus::Extracted);
    QVERIFY(doc->error.contains(QStringLiteral("could be checked")));
}

void TestStudyGeneration::stopsWhenModelUnavailable()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    MockLlmClient llm;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply {
        return LlmError{LlmErrorKind::NotRunning, QStringLiteral("Ollama is installed but not running.")};
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.joins, f.automata}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(llm.requests.size(), 1);   // no pointless retries
    QCOMPARE(finished.first().at(2).toString(), QStringLiteral("Ollama is installed but not running."));
    QCOMPARE(db.document(f.joins)->status, DocumentStatus::Extracted);
    QCOMPARE(db.document(f.automata)->status, DocumentStatus::Extracted);
    QVERIFY(!gen.isRunning());
}

void TestStudyGeneration::cancelsAndRegenerates()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db);
    MockLlmClient llm;
    llm.holdReplies = true;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply { return singleChoiceJson(); };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.automata}, {QuestionType::SingleChoice}, false));
    QVERIFY(llm.isBusy());
    gen.cancel();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(2).toString(), QStringLiteral("Generation cancelled."));
    QVERIFY(!llm.isBusy());
    llm.releaseHeld();   // a late reply after cancel is ignored
    QVERIFY(db.documentQuestions(f.automata).isEmpty());
    QCOMPARE(db.document(f.automata)->status, DocumentStatus::Extracted);

    // Regenerate replaces existing questions.
    Question old = parsed(QuestionType::MultiSelect, multiSelectJson());
    old.id = QStringLiteral("old-question");
    old.setId = f.studySet;
    old.documentId = f.automata;
    QVERIFY(db.saveQuestion(old, db.chunks(f.automata).first().id, QString()));
    MockLlmClient fresh;
    fresh.responder = [](const LlmRequest&) -> MockLlmClient::Reply { return multiSelectJson(); };
    QuestionGenerator regen(&db, &fresh);
    QSignalSpy done(&regen, &QuestionGenerator::finished);
    QVERIFY(regen.start({f.automata}, {QuestionType::MultiSelect}, true));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    const QVector<Question> now = db.documentQuestions(f.automata);
    QCOMPARE(now.size(), 1);
    QVERIFY(now.first().id != QStringLiteral("old-question"));
    QVERIFY(!db.question(QStringLiteral("old-question")));
}

QTEST_MAIN(TestStudyGeneration)
#include "test_study_generation.moc"
