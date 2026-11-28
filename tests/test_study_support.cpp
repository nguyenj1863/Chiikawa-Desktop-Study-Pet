#include "MockLlmClient.h"
#include "StudyTestUtils.h"

#include "app/AppSettings.h"
#include "study/ingest/DocumentExtractor.h"
#include "study/generation/QuestionGenerator.h"
#include "study/generation/QuestionValidator.h"
#include "study/practice/SetQuestionProvider.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"
#include "studyui/QuizPopup.h"
#include "studyui/SupportView.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

[[maybe_unused]] const bool kIsolatedSettings = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetSupportTests"));
    return true;
}();

QByteArray pngBytes(int w, int h, QColor color)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

const QString kCachePassage = QStringLiteral(
    "The cache sits between the CPU and main memory. A cache hit returns data in a few cycles, "
    "while a miss must fetch the line from memory. The table compares the access times of each level.");

const QVector<QStringList> kLatencyTable = {
    {QStringLiteral("Level"), QStringLiteral("Access time")},
    {QStringLiteral("L1 cache"), QStringLiteral("1 ns")},
    {QStringLiteral("Main memory"), QStringLiteral("100 ns")},
};

QString choiceJson(const QString& question, const QString& evidence, int visual)
{
    QJsonArray options;
    const QStringList texts = {QStringLiteral("1 ns"), QStringLiteral("10 ns"), QStringLiteral("50 ns"), QStringLiteral("100 ns")};
    for (int i = 0; i < 4; ++i) {
        options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)},
                                   {QStringLiteral("correct"), i == 0},
                                   {QStringLiteral("why"), QStringLiteral("From the access times.")}});
    }
    QJsonObject obj{
        {QStringLiteral("concept"), QStringLiteral("L1 access time")},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("question"), question},
        {QStringLiteral("options"), options},
        {QStringLiteral("explanation"), QStringLiteral("L1 is the fastest level.")},
        {QStringLiteral("difficulty"), QStringLiteral("easy")},
    };
    if (visual >= 0) {
        obj.insert(QStringLiteral("visual"), visual);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

const QString kHitPassage = QStringLiteral(
    "During the benchmark, 45 of 60 requests were cache hits and the rest went to main memory. "
    "The hit rate is the share of requests served by the cache, so a higher hit rate means fewer slow trips.");

/** A written-answer reply with a data snippet. */
QString writtenJson(const QString& question, const QString& data)
{
    QJsonObject obj{
        {QStringLiteral("concept"), QStringLiteral("Hit rate")},
        {QStringLiteral("evidence"), QStringLiteral("The hit rate is the share of requests served by the cache")},
        {QStringLiteral("data"), data},
        {QStringLiteral("question"), question},
        {QStringLiteral("expected_answer"), QStringLiteral("75%")},
        {QStringLiteral("key_points"), QJsonArray{QStringLiteral("45 / 60 = 0.75")}},
        {QStringLiteral("aliases"), QJsonArray{QStringLiteral("0.75")}},
        {QStringLiteral("explanation"), QStringLiteral("45 hits out of 60 requests is 75%.")},
        {QStringLiteral("difficulty"), QStringLiteral("medium")},
    };
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

Question choiceQuestion(const QString& prompt)
{
    Question q;
    q.id = QStringLiteral("q");
    q.type = QuestionType::SingleChoice;
    q.prompt = prompt;
    q.topic = QStringLiteral("topic");
    q.explanation = QStringLiteral("Because.");
    q.options = {{QStringLiteral("a"), QStringLiteral("Rows from the left table"), true, QString()},
                 {QStringLiteral("b"), QStringLiteral("Only matches"), false, QString()},
                 {QStringLiteral("c"), QStringLiteral("Nothing"), false, QString()},
                 {QStringLiteral("d"), QStringLiteral("Everything"), false, QString()}};
    q.sources.append({QStringLiteral("Deck"), QStringLiteral("slide 1"), QStringLiteral("A LEFT JOIN keeps every row of the left table."), 1, 0});
    return q;
}

} // namespace

class TestStudySupport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void slidesYieldFiguresAndTablesButNotLogos();
    void markdownTablesBecomeAssets();
    void importStoresAssetFilesAndRemovesThem();
    void questionsKeepTheirVisuals();
    void validatorRejectsUnseenVisuals();
    void generatorOffersVisualsAndAttachesTheUsedOne();
    void generatorRetriesTextOnlyAfterAnUnseenReference();
    void questionsMustNotLeanOnUnseenNotesOrData();
    void generatorShowsQuotedDataAndRetriesWithContext();
    void generatorRejectsInventedData();
    void questionsNeedingContextStayOutOfPractice();
    void figureRegionFindsTheFigureBand();
    void pdfFiguresAreCroppedFromThePage();
    void popupShowsTheFigureAndTable();

private:
    QTemporaryDir m_storage;
};

void TestStudySupport::initTestCase()
{
    QVERIFY(m_storage.isValid());
    StudyDatabase::setStorageDirectory(m_storage.path());
}

void TestStudySupport::cleanup()
{
    QDir(m_storage.path()).removeRecursively();
}

void TestStudySupport::slidesYieldFiguresAndTablesButNotLogos()
{
    using StudyTestUtils::TestSlide;
    const QByteArray figure = pngBytes(320, 200, QColor(30, 120, 200));
    const QByteArray logo = pngBytes(120, 120, Qt::red);
    const QByteArray icon = pngBytes(24, 24, Qt::green);
    TestSlide one{QStringLiteral("Memory hierarchy"), {QStringLiteral("Caches are small and fast.")}};
    one.picture = true;
    one.pictureData = figure;
    one.pictureAlt = QStringLiteral("Diagram of the memory hierarchy");
    one.logoData = logo;
    TestSlide two{QStringLiteral("Latency"), {QStringLiteral("Each level is slower than the one above it.")}};
    two.table = kLatencyTable;
    two.logoData = logo;
    TestSlide three{QStringLiteral("Summary"), {QStringLiteral("Locality makes caches work.")}};
    three.picture = true;
    three.pictureData = icon;   // too small to be a figure
    three.logoData = logo;

    const ExtractionResult r = DocumentExtractor::extractPptx(StudyTestUtils::buildPptx({one, two, three}));
    QVERIFY(r.ok);
    QCOMPARE(r.assets.size(), 2);   // the figure and the table; the repeated logo and the icon are skipped
    const auto image = std::find_if(r.assets.cbegin(), r.assets.cend(), [](const ExtractedAsset& a) { return a.kind == SupportKind::Image; });
    QVERIFY(image != r.assets.cend());
    QCOMPARE(image->unitKind, SegmentKind::Slide);
    QCOMPARE(image->unitNumber, 1);
    QCOMPARE(image->caption, QStringLiteral("Diagram of the memory hierarchy"));
    QCOMPARE(image->imageFormat, QStringLiteral("png"));
    QVERIFY(!QImage::fromData(image->imageData).isNull());
    const auto table = std::find_if(r.assets.cbegin(), r.assets.cend(), [](const ExtractedAsset& a) { return a.kind == SupportKind::Table; });
    QVERIFY(table != r.assets.cend());
    QCOMPARE(table->unitNumber, 2);
    QCOMPARE(table->table.headers, (QStringList{QStringLiteral("Level"), QStringLiteral("Access time")}));
    QCOMPARE(table->table.rows.size(), 2);
    QCOMPARE(table->table.rows.at(1).at(1), QStringLiteral("100 ns"));
    // The table text is still part of the slide's text.
    QVERIFY(r.segments.at(1).text.contains(QStringLiteral("100 ns")));
}

void TestStudySupport::markdownTablesBecomeAssets()
{
    const QByteArray md = "# Joins\nA LEFT JOIN keeps left rows.\n\n| Join | Keeps |\n|---|---|\n| INNER | matches |\n| LEFT | all left rows |\n\n# Next\nMore text here.\n";
    const ExtractionResult r = DocumentExtractor::extractText(md, true);
    QVERIFY(r.ok);
    QCOMPARE(r.assets.size(), 1);
    QCOMPARE(r.assets.first().kind, SupportKind::Table);
    QCOMPARE(r.assets.first().unitKind, SegmentKind::Section);
    QCOMPARE(r.assets.first().unitNumber, 1);
    QCOMPARE(r.assets.first().table.headers, (QStringList{QStringLiteral("Join"), QStringLiteral("Keeps")}));
    QCOMPARE(r.assets.first().table.rows.size(), 2);
    QCOMPARE(StudyText::tableToText(r.assets.first().table), QStringLiteral("Join | Keeps\nINNER | matches\nLEFT | all left rows"));
    QCOMPARE(StudyText::tableFromJson(StudyText::tableToJson(r.assets.first().table)).rows, r.assets.first().table.rows);
}

void TestStudySupport::importStoresAssetFilesAndRemovesThem()
{
    using StudyTestUtils::TestSlide;
    QTemporaryDir inputs;
    TestSlide one{QStringLiteral("Memory hierarchy"), {QStringLiteral("Caches are small and fast memories near the CPU.")}};
    one.picture = true;
    one.pictureData = pngBytes(300, 180, Qt::blue);
    TestSlide two{QStringLiteral("Latency"), {QStringLiteral("Each level is slower than the one above it.")}};
    two.table = kLatencyTable;
    const QString path = inputs.filePath(QStringLiteral("memory.pptx"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(StudyTestUtils::buildPptx({one, two}));
    f.close();

    StudyLibrary library;
    QVERIFY(library.isOpen());
    const qint64 set = library.database()->addStudySet(QStringLiteral("Systems")).value_or(0);
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    const QList<qint64> ids = library.importFiles(set, {path});
    QCOMPARE(ids.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 10000);
    const QVector<SupportAsset> assets = library.database()->supportAssets(ids.first());
    QCOMPARE(assets.size(), 2);
    const SupportAsset image = assets.first().kind == SupportKind::Image ? assets.first() : assets.last();
    QVERIFY(QFileInfo::exists(image.path));
    QCOMPARE(image.width, 300);
    QCOMPARE(image.height, 180);
    QCOMPARE(library.database()->supportAssets(ids.first(), 2).size(), 1);   // per slide

    const QString folder = StudyDatabase::assetsDirectory(ids.first());
    QVERIFY(QDir(folder).exists());
    QVERIFY(library.removeDocument(ids.first()));
    QVERIFY(!QDir(folder).exists());
    QVERIFY(library.database()->supportAssets(ids.first()).isEmpty());
}

void TestStudySupport::questionsKeepTheirVisuals()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Deck"), QStringLiteral("d.pptx"), DocumentKind::Pptx).value_or(0);
    SupportAsset table;
    table.documentId = doc;
    table.unitKind = SegmentKind::Slide;
    table.unitNumber = 2;
    table.kind = SupportKind::Table;
    table.table.headers = kLatencyTable.first();
    table.table.rows = kLatencyTable.mid(1);
    const qint64 assetId = db.addSupportAsset(table).value_or(0);
    QVERIFY(assetId > 0);

    Question q = choiceQuestion(QStringLiteral("According to the table, how fast is L1?"));
    q.id = QStringLiteral("with-table");
    q.setId = set;
    q.documentId = doc;
    SupportRef ref;
    ref.kind = SupportKind::Table;
    ref.assetId = assetId;
    q.support.append(ref);
    SupportRef data;
    data.kind = SupportKind::Data;
    data.text = QStringLiteral("L1: 1 ns, memory: 100 ns");
    q.support.append(data);
    q.evidenceFromVisual = true;
    QVERIFY(db.saveQuestion(q, 0, QStringLiteral("test")));

    const auto loaded = db.question(q.id);
    QVERIFY(loaded);
    QCOMPARE(loaded->support.size(), 2);
    QCOMPARE(loaded->support.at(0).assetId, assetId);
    QCOMPARE(loaded->support.at(0).table.rows.size(), 2);   // resolved from the stored asset
    QCOMPARE(loaded->support.at(1).text, QStringLiteral("L1: 1 ns, memory: 100 ns"));
    QVERIFY(loaded->evidenceFromVisual);
    QCOMPARE(db.setQuestions(set, {}, false).first().support.size(), 2);

    // An asset that disappears (document re-read) is dropped rather than shown empty.
    QVERIFY(db.removeSupportAssets(doc));
    QCOMPARE(db.question(q.id)->support.size(), 1);
}

void TestStudySupport::validatorRejectsUnseenVisuals()
{
    using QuestionValidator::referencesVisual;
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("According to the figure, which layer caches pages?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("What does Figure 3 show about latency?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("Based on the diagram above, where does the request go?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("Using the provided data, which level is fastest?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("Which value is shown in the table below?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("According to the table, which level is fastest?"))));
    QVERIFY(referencesVisual(choiceQuestion(QStringLiteral("Using the data, what is the average latency?"))));
    QVERIFY(!referencesVisual(choiceQuestion(QStringLiteral("Which columns are in the table after a LEFT JOIN?"))));
    QVERIFY(!referencesVisual(choiceQuestion(QStringLiteral("Which rows are removed from the table by DELETE?"))));
    // Ordinary subject words are fine.
    QVERIFY(!referencesVisual(choiceQuestion(QStringLiteral("Which rows does a LEFT JOIN keep from the left table?"))));
    QVERIFY(!referencesVisual(choiceQuestion(QStringLiteral("What does the data link layer do?"))));
    QVERIFY(!referencesVisual(choiceQuestion(QStringLiteral("Why do databases index a table's primary key?"))));

    const QString passage = QStringLiteral("A LEFT JOIN keeps every row of the left table. Figure 3 shows an example.");
    Question unseen = choiceQuestion(QStringLiteral("What does the figure show about the LEFT JOIN result?"));
    const auto rejected = QuestionValidator::validate(unseen, passage);
    QVERIFY(!rejected.ok);
    QCOMPARE(rejected.reason, QString::fromLatin1(QuestionValidator::kUnseenVisualReason));

    SupportRef table;
    table.kind = SupportKind::Table;
    table.table.headers = {QStringLiteral("Join")};
    table.table.rows = {{QStringLiteral("LEFT")}};
    Question shown = unseen;
    shown.prompt = QStringLiteral("Which join does the table list for keeping every left row?");
    shown.support.append(table);
    QVERIFY2(QuestionValidator::validate(shown, passage).ok, qPrintable(QuestionValidator::validate(shown, passage).reason));
    shown.support.first().table = TableData();   // nothing left to show
    QVERIFY(!QuestionValidator::validate(shown, passage).ok);
}

void TestStudySupport::generatorOffersVisualsAndAttachesTheUsedOne()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Memory"), QStringLiteral("m.pptx"), DocumentKind::Pptx).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Slide, 2, 0, kCachePassage}}));
    db.setDocumentStatus(doc, DocumentStatus::Extracted);
    SupportAsset table;
    table.documentId = doc;
    table.unitKind = SegmentKind::Slide;
    table.unitNumber = 2;
    table.kind = SupportKind::Table;
    table.table.headers = kLatencyTable.first();
    table.table.rows = kLatencyTable.mid(1);
    QVERIFY(db.addSupportAsset(table));
    SupportAsset uninterpreted;   // a picture nobody has described yet is not offered to the model
    uninterpreted.documentId = doc;
    uninterpreted.unitKind = SegmentKind::Slide;
    uninterpreted.unitNumber = 2;
    uninterpreted.kind = SupportKind::Image;
    uninterpreted.path = QStringLiteral("x.png");
    QVERIFY(db.addSupportAsset(uninterpreted));

    MockLlmClient llm;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply {
        return choiceJson(QStringLiteral("According to the table, what is the access time of the L1 cache?"),
                          QStringLiteral("L1 cache | 1 ns"), 1);
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);

    QCOMPARE(llm.requests.size(), 1);
    const LlmRequest& request = llm.requests.first();
    QVERIFY(request.prompt.contains(QStringLiteral("VISUALS ON THE SAME SLIDE")));
    QVERIFY(request.prompt.contains(QStringLiteral("Main memory | 100 ns")));
    QVERIFY(!request.prompt.contains(QStringLiteral("[2]")));   // only the table is offered
    QVERIFY(request.schema.contains("\"visual\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1}"));
    QVERIFY(request.schema.indexOf("\"visual\"") < request.schema.indexOf("\"evidence\""));

    const QVector<Question> saved = db.documentQuestions(doc);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().support.size(), 1);
    QCOMPARE(saved.first().support.first().kind, SupportKind::Table);
    QCOMPARE(saved.first().support.first().table.rows.size(), 2);
    QVERIFY(saved.first().evidenceFromVisual);   // the evidence quotes the table, not the passage

    // Without visuals the schema has no "visual" field and the prompt forbids pointing at any.
    const LlmRequest plain = QuestionGenerator::buildRequest(QuestionType::SingleChoice, {0, doc, set, SegmentKind::Slide, 9, 0, kCachePassage},
                                                             QStringLiteral("Memory"), {});
    QVERIFY(!plain.schema.contains("\"visual\""));
    QVERIFY(plain.prompt.contains(QStringLiteral("do not refer to any figure")));
}

void TestStudySupport::generatorRetriesTextOnlyAfterAnUnseenReference()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Memory"), QStringLiteral("m.pdf"), DocumentKind::Pdf).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Page, 1, 0, kCachePassage}}));
    db.setDocumentStatus(doc, DocumentStatus::Extracted);

    MockLlmClient llm;
    int calls = 0;
    llm.responder = [&calls](const LlmRequest&) -> MockLlmClient::Reply {
        ++calls;
        if (calls == 1) {
            // Points at a figure the student would never see.
            return choiceJson(QStringLiteral("According to the figure above, where does the cache sit?"),
                              QStringLiteral("The cache sits between the CPU and main memory."), -1);
        }
        return choiceJson(QStringLiteral("Where does the cache sit?"), QStringLiteral("The cache sits between the CPU and main memory."), -1);
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(calls, 2);
    QCOMPARE(finished.first().at(0).toInt(), 1);   // the rewritten question was accepted
    const QVector<Question> saved = db.documentQuestions(doc);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().prompt, QStringLiteral("Where does the cache sit?"));
    QVERIFY(saved.first().support.isEmpty());
}

void TestStudySupport::questionsMustNotLeanOnUnseenNotesOrData()
{
    using QuestionValidator::contextProblem;
    const QString vague = QString::fromLatin1(QuestionValidator::kVagueSourceReason);
    const QString unseen = QString::fromLatin1(QuestionValidator::kUnseenVisualReason);
    const QString inputs = QString::fromLatin1(QuestionValidator::kMissingInputsReason);

    // The example that prompted these rules.
    QVERIFY(!contextProblem(choiceQuestion(
        QStringLiteral("According to your notes, what is the correct percentage based on the observed data?"))).isEmpty());
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which join keeps unmatched rows, as mentioned earlier?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which join do the lecture slides recommend?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which join is described in the lecture?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which join does your textbook describe first?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which group improved according to the measured values?"))), unseen);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("In the experiment, which group improved?"))), unseen);
    // Seen in a real lecture: pointing at slide content without naming it.
    QCOMPARE(contextProblem(choiceQuestion(
                 QStringLiteral("According to the provided derivations for the PCP instance, what is the final string?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("What are the examples of undecidable problems listed?"))), vague);
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which problems are undecidable?"))), QString());
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which of the following statements about P are true?"))), QString());

    // Self-contained questions and ordinary subject words pass.
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("In the Milgram experiment, what were participants told to do?"))), QString());
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("What does a cursor in the text field show?"))), QString());
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which codon starts translation in the reading frame?"))), QString());
    QCOMPARE(contextProblem(choiceQuestion(QStringLiteral("Which rows does a LEFT JOIN keep from the left table?"))), QString());

    // Asking for a value needs the numbers it comes from.
    Question rate = choiceQuestion(QStringLiteral("Calculate the hit rate of the cache."));
    rate.options.first().text = QStringLiteral("75%");
    QCOMPARE(contextProblem(rate), inputs);
    rate.prompt = QStringLiteral("A cache served 45 of 60 requests. Calculate its hit rate.");
    QCOMPARE(contextProblem(rate), QString());
    rate.prompt = QStringLiteral("Calculate the hit rate of the cache.");
    SupportRef data;
    data.kind = SupportKind::Data;
    data.text = QStringLiteral("45 of 60 requests were cache hits");
    rate.support.append(data);
    QCOMPARE(contextProblem(rate), QString());
    Question recall = choiceQuestion(QStringLiteral("How many bones are in the adult human body?"));
    recall.options.first().text = QStringLiteral("206");
    QCOMPARE(contextProblem(recall), QString());   // recall of a stated fact stands on its own

    // "According to your notes," is cut from generated questions.
    QString error;
    const auto parsed = QuestionGenerator::parseResponse(
        QuestionType::SingleChoice,
        choiceJson(QStringLiteral("According to your notes, what is the access time of the L1 cache?"),
                   QStringLiteral("L1 cache | 1 ns"), -1),
        &error);
    QVERIFY2(parsed, qPrintable(error));
    QCOMPARE(parsed->prompt, QStringLiteral("What is the access time of the L1 cache?"));

    // Answer choices repeated in the question ("(a) ... (b) ...", as a 9B model wrote for a quiz
    // slide) are cut, on their own lines or inline; the choices themselves stay as options.
    const QString stem = QStringLiteral("Which of the following represents a correct ordering of complexity?");
    for (const QString& repeated : {stem + QStringLiteral("\n(a) 1 < lgn < n^3\n(b) n < n^2 < n!\n(c) lgn < n\n(d) n < n!"),
                                    stem + QStringLiteral(" (a) 1 < lgn < n^3 (b) n < n^2 < n! (c) lgn < n (d) n < n!")}) {
        const auto cut = QuestionGenerator::parseResponse(QuestionType::SingleChoice,
                                                          choiceJson(repeated, QStringLiteral("L1 cache | 1 ns"), -1), &error);
        QVERIFY2(cut, qPrintable(error));
        QCOMPARE(cut->prompt, stem);
    }
    // A label inside the question that is not a list of choices stays.
    const QString keep = QStringLiteral("What does step (a) of the merge sort algorithm do with the input?");
    const auto kept = QuestionGenerator::parseResponse(QuestionType::SingleChoice,
                                                       choiceJson(keep, QStringLiteral("L1 cache | 1 ns"), -1), &error);
    QVERIFY2(kept, qPrintable(error));
    QCOMPARE(kept->prompt, keep);
}

void TestStudySupport::generatorShowsQuotedDataAndRetriesWithContext()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Caches"), QStringLiteral("c.txt"), DocumentKind::Text).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Section, 1, 0, kHitPassage}}));
    db.setDocumentStatus(doc, DocumentStatus::Extracted);

    MockLlmClient llm;
    int calls = 0;
    llm.responder = [&calls](const LlmRequest&) -> MockLlmClient::Reply {
        ++calls;
        if (calls == 1) {
            return writtenJson(QStringLiteral("As mentioned earlier, what is the hit rate of the cache?"), QString());
        }
        return writtenJson(QStringLiteral("Calculate the hit rate of the cache."),
                           QStringLiteral("45 of 60 requests were cache hits"));
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({doc}, {QuestionType::Written}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(calls, 2);
    QVERIFY(llm.requests.first().system.contains(QStringLiteral("never write \"your notes\"")));
    QVERIFY(llm.requests.first().schema.contains("\"data\""));
    QVERIFY(!llm.requests.first().prompt.contains(QStringLiteral("cannot see")));
    QVERIFY(llm.requests.at(1).prompt.contains(QStringLiteral("depended on information the student cannot see")));

    const QVector<Question> saved = db.documentQuestions(doc);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().support.size(), 1);
    QCOMPARE(saved.first().support.first().kind, SupportKind::Data);
    QCOMPARE(saved.first().support.first().text, QStringLiteral("45 of 60 requests were cache hits"));
}

void TestStudySupport::generatorRejectsInventedData()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Caches"), QStringLiteral("c.txt"), DocumentKind::Text).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Section, 1, 0, kHitPassage}}));
    db.setDocumentStatus(doc, DocumentStatus::Extracted);

    MockLlmClient llm;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply {
        // 48 never appears in the material: shown numbers must be quoted, not made up.
        return writtenJson(QStringLiteral("Calculate the hit rate of the cache."),
                           QStringLiteral("48 of 60 requests were cache hits"));
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({doc}, {QuestionType::Written}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(llm.requests.size(), QuestionGenerator::kMaxAttemptsPerChunk);
    QVERIFY(db.documentQuestions(doc).isEmpty());
}

void TestStudySupport::questionsNeedingContextStayOutOfPractice()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Databases")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Joins"), QStringLiteral("j.pptx"), DocumentKind::Pptx).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Slide, 1, 0, QStringLiteral("A LEFT JOIN keeps every row of the left table.")}}));
    const qint64 chunk = db.chunks(doc).first().id;

    Question good = choiceQuestion(QStringLiteral("Which rows does a LEFT JOIN keep?"));
    good.id = QStringLiteral("good");
    good.documentId = doc;
    good.setId = set;
    Question vague = choiceQuestion(QStringLiteral("Which join keeps every row, according to your notes?"));
    vague.id = QStringLiteral("vague");
    vague.documentId = doc;
    vague.setId = set;
    Question edited = vague;   // once the student has edited it, it is theirs to word
    edited.id = QStringLiteral("edited");
    edited.prompt = QStringLiteral("Which join keeps every row, as discussed in class?");
    edited.origin = QuestionOrigin::Edited;
    for (const Question& q : {good, vague, edited}) {
        QVERIFY(db.saveQuestion(q, chunk, QStringLiteral("test")));
    }
    QVERIFY(QuestionValidator::needsContext(*db.question(QStringLiteral("vague"))));
    QVERIFY(!QuestionValidator::needsContext(*db.question(QStringLiteral("edited"))));

    // Kept in the library (to fix or delete), left out of practice and check-ins.
    QCOMPARE(db.documentQuestions(doc).size(), 3);
    SetQuestionProvider provider(&db, nullptr);
    QCOMPARE(provider.countFor(set, {}), 2);
    QCOMPARE(provider.start(set, {}), 2);
    QStringList served;
    while (const auto q = provider.nextQuestion({})) {
        served.append(q->id);
    }
    QVERIFY(!served.contains(QStringLiteral("vague")));
}

void TestStudySupport::figureRegionFindsTheFigureBand()
{
    using DocumentExtractor::figureRegion;
    const QSizeF page(600, 800);
    // Body text at the top and bottom, a diagram (with short labels) between, its caption under it.
    const QList<QRectF> lines = {QRectF(50, 50, 500, 12), QRectF(50, 70, 480, 12), QRectF(200, 200, 40, 10),
                                 QRectF(300, 300, 50, 10), QRectF(50, 600, 500, 12), QRectF(50, 620, 500, 12)};
    const QRectF figureCaption(50, 560, 200, 12);
    const QRectF figure = figureRegion(page, figureCaption, lines, false);
    QVERIFY(figure.isValid());
    QCOMPARE(figure.top(), 82.0);           // just below the last body line above it
    QCOMPARE(figure.bottom(), 572.0);       // the caption is included
    QVERIFY(figure.contains(QRectF(200, 200, 40, 10)));   // labels inside the diagram stay in

    // A table caption sits above its table.
    const QRectF tableCaption(50, 90, 200, 12);
    const QRectF table = figureRegion(page, tableCaption, lines, true);
    QCOMPARE(table.top(), 90.0);
    QCOMPARE(table.bottom(), 600.0);

    // No room for a figure next to the caption: keep the whole page.
    const QList<QRectF> dense = {QRectF(50, 540, 500, 12), QRectF(50, 580, 500, 12)};
    QVERIFY(!figureRegion(page, QRectF(50, 560, 200, 12), dense, false).isValid());
    QVERIFY(!figureRegion(page, QRectF(), lines, false).isValid());
}

void TestStudySupport::pdfFiguresAreCroppedFromThePage()
{
    if (!DocumentExtractor::pdfSupported()) {
        QSKIP("QtPdf is not available in this build");
    }
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("figure.pdf"));
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter p(&writer);
        QFont font = p.font();
        font.setPixelSize(12);
        p.setFont(font);
        const QString body = QStringLiteral("Caches keep recently used data close to the processor so most requests are fast.");
        p.drawText(QPointF(40, 60), body);
        p.drawText(QPointF(40, 80), body);
        p.setPen(QPen(Qt::black, 3));
        p.drawRect(QRectF(120, 140, 300, 260));
        p.drawText(QPointF(40, 440), QStringLiteral("Figure 1: The cache hierarchy"));
        p.drawText(QPointF(40, 600), body);
        p.drawText(QPointF(40, 620), body);
    }
    const ExtractionResult r = DocumentExtractor::extractPdf(path);
    QVERIFY(r.ok);
    QCOMPARE(r.assets.size(), 1);
    QCOMPARE(r.assets.first().caption, QStringLiteral("Figure 1: The cache hierarchy"));
    QImage image;
    QVERIFY(image.loadFromData(r.assets.first().imageData));
    // The whole page would be 1400 px tall; the crop keeps the drawing and its caption only.
    QVERIFY2(image.height() < 900, qPrintable(QString::number(image.height())));
    QVERIFY(image.height() > 300);
}

void TestStudySupport::popupShowsTheFigureAndTable()
{
    QTemporaryDir dir;
    const QString imagePath = dir.filePath(QStringLiteral("figure.png"));
    QImage(400, 200, QImage::Format_RGB32).save(imagePath);

    Question q = choiceQuestion(QStringLiteral("Which component does the diagram place between the CPU and memory?"));
    SupportRef figure;
    figure.kind = SupportKind::Image;
    figure.imagePath = imagePath;
    figure.caption = QStringLiteral("Figure 1: Memory hierarchy");
    SupportRef table;
    table.kind = SupportKind::Table;
    table.table.headers = kLatencyTable.first();
    table.table.rows = kLatencyTable.mid(1);
    q.support = {figure, table};

    QuizPopup popup;
    popup.showQuestion(q, QRect(700, 600, 120, 120));
    QVERIFY(popup.supportView()->isVisibleTo(&popup));
    QCOMPARE(popup.supportView()->images().size(), 1);
    QCOMPARE(popup.supportView()->tables().size(), 1);
    QVERIFY(popup.supportView()->tables().first()->text().contains(QStringLiteral("100 ns")));
    // Every visual names where it comes from, so the page or slide reference stays in view.
    QCOMPARE(popup.supportView()->captions().size(), 2);
    QCOMPARE(popup.supportView()->captions().at(0)->text(), QStringLiteral("Figure 1: Memory hierarchy · slide 1"));
    QCOMPARE(popup.supportView()->captions().at(1)->text(), QStringLiteral("From slide 1"));
    // A whole page is named once ("From p. 2"), not "Page 2 · p. 2".
    SupportRef page;
    page.caption = QStringLiteral("Page 2");
    QCOMPARE(SupportView::captionFor(page, QStringLiteral("p. 2")), QStringLiteral("From p. 2"));
    page.caption = QStringLiteral("Page 24 (scanned)");
    QCOMPARE(SupportView::captionFor(page, QStringLiteral("p. 24 (scanned)")), QStringLiteral("From p. 24 (scanned)"));
    // The image keeps its aspect ratio and fits the bubble width.
    SupportImage* image = popup.supportView()->images().first();
    QCOMPARE(image->heightForWidth(300), 150);
    QCOMPARE(image->heightForWidth(1000), SupportImage::kMaxHeight);
    QVERIFY(popup.height() <= QuizPopup::kMaxHeight);

    // A question without visuals hides the block again.
    popup.showQuestion(choiceQuestion(QStringLiteral("Which rows does a LEFT JOIN keep?")), QRect(700, 600, 120, 120));
    QVERIFY(!popup.supportView()->isVisibleTo(&popup));
    popup.dismiss();
}

QTEST_MAIN(TestStudySupport)
#include "test_study_support.moc"
