#include "MockLlmClient.h"

#include "study/ingest/FigureInterpreter.h"
#include "study/generation/QuestionGenerator.h"
#include "study/StudyDatabase.h"

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kSlideText = QStringLiteral(
    "The memory hierarchy trades speed for size. Requests that miss in the cache continue to main memory, "
    "which is slower but much larger.");

QString figureJson(const QString& kind = QStringLiteral("diagram"), bool legible = true,
                   const QStringList& elements = {QStringLiteral("CPU: issues loads"), QStringLiteral("Cache: small fast memory"),
                                                  QStringLiteral("Main memory: large slow memory")})
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("legible"), legible},
        {QStringLiteral("title"), QStringLiteral("Memory hierarchy")},
        {QStringLiteral("readable_text"), QStringLiteral("CPU Cache Main memory")},
        {QStringLiteral("summary"), QStringLiteral("Arrows show a load request going from the CPU to the cache, and on a miss to main memory.")},
        {QStringLiteral("elements"), QJsonArray::fromStringList(elements)},
        {QStringLiteral("relationships"), QJsonArray{QStringLiteral("CPU -> Cache: load request"),
                                                     QStringLiteral("Cache -> Main memory: on a miss")}},
        {QStringLiteral("table_rows"), QJsonArray{}},
        {QStringLiteral("values"), QJsonArray{}},
    }).toJson(QJsonDocument::Compact));
}

QString questionJson(int visual, const QString& evidence)
{
    QJsonArray options;
    const QStringList texts = {QStringLiteral("Main memory"), QStringLiteral("The CPU"), QStringLiteral("The disk"), QStringLiteral("The cache again")};
    for (int i = 0; i < 4; ++i) {
        options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)}, {QStringLiteral("correct"), i == 0},
                                   {QStringLiteral("why"), QStringLiteral("Follow the arrows.")}});
    }
    QJsonObject obj{
        {QStringLiteral("concept"), QStringLiteral("cache miss path")},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("question"), QStringLiteral("In the figure, where does a load go after it misses in the cache?")},
        {QStringLiteral("options"), options},
        {QStringLiteral("explanation"), QStringLiteral("A miss continues to main memory.")},
        {QStringLiteral("difficulty"), QStringLiteral("easy")},
    };
    if (visual >= 0) {
        obj.insert(QStringLiteral("visual"), visual);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

struct Fixture {
    qint64 set = 0;
    qint64 doc = 0;
    qint64 figure = 0;
};

Fixture seed(StudyDatabase& db, const QString& dir)
{
    Fixture f;
    f.set = db.addStudySet(QStringLiteral("Systems")).value_or(0);
    f.doc = db.addDocument(f.set, QStringLiteral("Memory"), QStringLiteral("m.pptx"), DocumentKind::Pptx).value_or(0);
    db.replaceChunks(f.doc, {{0, 0, 0, SegmentKind::Slide, 2, 0, kSlideText}});
    db.setDocumentStatus(f.doc, DocumentStatus::Extracted);
    QImage image(900, 500, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QString path = dir + QStringLiteral("/figure.png");
    image.save(path);
    SupportAsset asset;
    asset.documentId = f.doc;
    asset.unitKind = SegmentKind::Slide;
    asset.unitNumber = 2;
    asset.kind = SupportKind::Image;
    asset.path = path;
    asset.width = 900;
    asset.height = 500;
    f.figure = db.addSupportAsset(asset).value_or(0);
    return f;
}

} // namespace

class TestStudyFigures : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void readingsParseAndFormat();
    void decorativeAndContradictedFiguresAreNotTrusted();
    void requestsCarryTheImage();
    void figuresAreReadOnceThenQuestionsUseThem();
    void unreadableFiguresStayOutOfQuestions();
    void missingVisionModelKeepsGeneratingFromText();
    void readFiguresReadsAWholeDocument();

private:
    QTemporaryDir m_storage;
    QTemporaryDir m_files;
};

void TestStudyFigures::initTestCase()
{
    QVERIFY(m_storage.isValid());
    StudyDatabase::setStorageDirectory(m_storage.path());
}

void TestStudyFigures::cleanup()
{
    QDir(m_storage.path()).removeRecursively();
}

void TestStudyFigures::readingsParseAndFormat()
{
    QString error;
    const auto reading = FigureInterpreter::parse(QStringLiteral("```json\n") + figureJson() + QStringLiteral("\n```"), &error);
    QVERIFY2(reading, qPrintable(error));
    QCOMPARE(reading->kind, QStringLiteral("diagram"));
    QVERIFY(reading->legible);
    QCOMPARE(reading->elements.size(), 3);
    QCOMPARE(reading->relationships.size(), 2);
    const QString text = reading->toText();
    QVERIFY(text.startsWith(QStringLiteral("A diagram: Memory hierarchy.")));
    QVERIFY(text.contains(QStringLiteral("Connections: CPU -> Cache: load request")));

    // Tables seen in pictures come back as real tables.
    QJsonObject table = QJsonDocument::fromJson(figureJson(QStringLiteral("table")).toUtf8()).object();
    table.insert(QStringLiteral("table_rows"), QJsonArray{QJsonArray{QStringLiteral("Level"), QStringLiteral("Time")},
                                                         QJsonArray{QStringLiteral("L1"), QStringLiteral("1 ns")}});
    const auto tableReading = FigureInterpreter::parse(QString::fromUtf8(QJsonDocument(table).toJson()));
    QVERIFY(tableReading);
    QCOMPARE(tableReading->table.headers, (QStringList{QStringLiteral("Level"), QStringLiteral("Time")}));
    QCOMPARE(tableReading->table.rows.size(), 1);
    QVERIFY(tableReading->toText().contains(QStringLiteral("L1 | 1 ns")));

    QVERIFY(!FigureInterpreter::parse(QStringLiteral("no json here")));
}

void TestStudyFigures::decorativeAndContradictedFiguresAreNotTrusted()
{
    QVERIFY(!FigureInterpreter::parse(figureJson(QStringLiteral("decorative")))->legible);
    QVERIFY(!FigureInterpreter::parse(figureJson(QStringLiteral("photo")))->legible);
    QVERIFY(!FigureInterpreter::parse(figureJson(QStringLiteral("diagram"), false))->legible);
    // Seen live: a small model called unlabelled clip art a legible "diagram". No words, values or cells: not used.
    QJsonObject clipArt = QJsonDocument::fromJson(figureJson().toUtf8()).object();
    clipArt.insert(QStringLiteral("readable_text"), QString());
    clipArt.insert(QStringLiteral("summary"), QStringLiteral("A simple cartoon face with two eyes and pink cheeks."));
    clipArt.insert(QStringLiteral("elements"), QJsonArray{QStringLiteral("left_eye"), QStringLiteral("right_eye")});
    QVERIFY(!FigureInterpreter::parse(QString::fromUtf8(QJsonDocument(clipArt).toJson()))->legible);

    const FigureInterpreter::Reading reading = *FigureInterpreter::parse(figureJson());
    QVERIFY(FigureInterpreter::agreesWithOcr(reading, QStringLiteral("CPU  Cache  Main memory  load request")));
    QVERIFY(FigureInterpreter::agreesWithOcr(reading, QString()));   // nothing to compare with
    QVERIFY(!FigureInterpreter::agreesWithOcr(reading, QStringLiteral("Mitochondria Nucleus Ribosome Cytoplasm membrane")));
}

void TestStudyFigures::requestsCarryTheImage()
{
    QImage big(3000, 1500, QImage::Format_ARGB32);
    big.fill(Qt::transparent);
    const QString path = m_files.filePath(QStringLiteral("big.png"));
    QVERIFY(big.save(path));
    const QByteArray jpeg = FigureInterpreter::prepareImage(path);
    QVERIFY(!jpeg.isEmpty());
    const QImage sent = QImage::fromData(jpeg);
    QCOMPARE(sent.width(), FigureInterpreter::kMaxImageEdge);
    QCOMPARE(sent.pixelColor(10, 10), QColor(Qt::white));   // transparency flattened onto white

    const LlmRequest r = FigureInterpreter::buildRequest(jpeg, QStringLiteral("Figure 2: Memory"), QStringLiteral("qwen3-vl:4b"));
    QCOMPARE(r.images.size(), 1);
    QCOMPARE(r.model, QStringLiteral("qwen3-vl:4b"));
    QVERIFY(r.prompt.contains(QStringLiteral("Figure 2: Memory")));
    QVERIFY(r.schema.indexOf("\"legible\"") < r.schema.indexOf("\"summary\""));
    QVERIFY(QJsonDocument::fromJson(r.schema).isObject());
}

void TestStudyFigures::figuresAreReadOnceThenQuestionsUseThem()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db, m_files.path());
    MockLlmClient llm;
    llm.responder = [](const LlmRequest& r) -> MockLlmClient::Reply {
        if (!r.images.isEmpty()) {
            return figureJson();
        }
        return questionJson(1, QStringLiteral("Cache -> Main memory: on a miss"));
    };
    QuestionGenerator gen(&db, &llm);
    gen.setVisionModel(QStringLiteral("vision-model"));
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QSignalSpy progress(&gen, &QuestionGenerator::progress);
    QVERIFY(gen.start({f.doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);

    QCOMPARE(llm.requests.size(), 2);
    QCOMPARE(llm.requests.at(0).images.size(), 1);           // first: look at the figure
    QCOMPARE(llm.requests.at(0).model, QStringLiteral("vision-model"));
    QVERIFY(llm.requests.at(1).images.isEmpty());             // then: write the question
    QVERIFY(llm.requests.at(1).prompt.contains(QStringLiteral("Cache -> Main memory: on a miss")));
    bool looked = false;
    for (const QList<QVariant>& args : std::as_const(progress)) {
        looked = looked || args.at(2).toString().contains(QStringLiteral("Looking at the figure on slide 2"));
    }
    QVERIFY(looked);

    const auto asset = db.supportAsset(f.figure);
    QVERIFY(asset->interpreted);
    QVERIFY(asset->legible);
    QCOMPARE(asset->interpretedBy, QStringLiteral("vision-model"));
    QCOMPARE(asset->text, QStringLiteral("CPU Cache Main memory"));
    const QVector<Question> saved = db.documentQuestions(f.doc);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().support.size(), 1);
    QCOMPARE(saved.first().support.first().kind, SupportKind::Image);
    QVERIFY(!saved.first().support.first().interpretation.isEmpty());
    QVERIFY(saved.first().evidenceFromVisual);

    // A second run does not look at the same figure again.
    QVERIFY(db.replaceChunks(f.doc, {{0, 0, 0, SegmentKind::Slide, 2, 0, kSlideText + QStringLiteral(" Locality keeps hit rates high.")}}));
    llm.requests.clear();
    QVERIFY(gen.start({f.doc}, {QuestionType::SingleChoice}, true));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
    for (const LlmRequest& r : std::as_const(llm.requests)) {
        QVERIFY(r.images.isEmpty());
    }
}

void TestStudyFigures::unreadableFiguresStayOutOfQuestions()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db, m_files.path());
    MockLlmClient llm;
    llm.responder = [](const LlmRequest& r) -> MockLlmClient::Reply {
        if (!r.images.isEmpty()) {
            return figureJson(QStringLiteral("decorative"), false);
        }
        return questionJson(-1, QStringLiteral("Requests that miss in the cache continue to main memory"));
    };
    QuestionGenerator gen(&db, &llm);
    gen.setVisionModel(QStringLiteral("vision-model"));
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(db.supportAsset(f.figure)->interpreted);
    QVERIFY(!db.supportAsset(f.figure)->legible);
    const LlmRequest& question = llm.requests.last();
    QVERIFY(!question.prompt.contains(QStringLiteral("VISUALS ON THE SAME")));   // nothing usable to offer
    // The reply says "In the figure" without a visual, so it is retried text-only and then dropped.
    QVERIFY(db.documentQuestions(f.doc).isEmpty());
}

void TestStudyFigures::missingVisionModelKeepsGeneratingFromText()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db, m_files.path());
    MockLlmClient llm;
    llm.responder = [](const LlmRequest& r) -> MockLlmClient::Reply {
        if (!r.images.isEmpty()) {
            return LlmError{LlmErrorKind::ModelMissing, QStringLiteral("model not found")};
        }
        QJsonObject obj = QJsonDocument::fromJson(questionJson(-1, QStringLiteral("Requests that miss in the cache continue to main memory")).toUtf8()).object();
        obj.insert(QStringLiteral("question"), QStringLiteral("Where does a load go after it misses in the cache?"));
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    };
    QuestionGenerator gen(&db, &llm);
    gen.setVisionModel(QStringLiteral("not-installed"));
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({f.doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(0).toInt(), 1);   // the text question was still written
    QVERIFY(!db.supportAsset(f.figure)->interpreted);   // and the figure can be read later

    // Without a vision model nothing is sent to look at images at all.
    QuestionGenerator noVision(&db, &llm);
    llm.requests.clear();
    QVERIFY(!noVision.readFigures(f.doc));
    QVERIFY(llm.requests.isEmpty());
}

void TestStudyFigures::readFiguresReadsAWholeDocument()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const Fixture f = seed(db, m_files.path());
    MockLlmClient llm;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply { return figureJson(); };
    QuestionGenerator gen(&db, &llm);
    gen.setVisionModel(QStringLiteral("vision-model"));
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.readFigures(f.doc));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(2).toString(), QStringLiteral("Read 1 figure."));
    QVERIFY(db.supportAsset(f.figure)->interpreted);
    QVERIFY(db.documentQuestions(f.doc).isEmpty());   // reading figures writes no questions
    QVERIFY(!gen.readFigures(f.doc));                 // nothing left to read
}

QTEST_MAIN(TestStudyFigures)
#include "test_study_figures.moc"
