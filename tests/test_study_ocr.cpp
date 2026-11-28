#include "MockLlmClient.h"
#include "StudyTestUtils.h"

#include "app/AppSettings.h"
#include "study/ingest/DocumentExtractor.h"
#include "study/ingest/OcrEngine.h"
#include "study/generation/QuestionGenerator.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

[[maybe_unused]] const bool kIsolatedSettings = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetOcrTests"));
    return true;
}();

const QString kScannedText = QStringLiteral(
    "Mitochondria produce most of the energy a cell needs. They turn glucose and oxygen into ATP "
    "through cellular respiration.");

/** Stands in for the OS engine: returns fixed text for every image and counts calls. */
class FakeOcrEngine final : public OcrEngine {
public:
    FakeOcrEngine(QString text, double confidence, bool available, int* calls)
        : m_text(std::move(text)), m_confidence(confidence), m_available(available), m_calls(calls) {}
    [[nodiscard]] bool available() const override { return m_available; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Fake OCR"); }
    [[nodiscard]] QStringList languages() const override { return {QStringLiteral("en-US")}; }
    [[nodiscard]] OcrResult recognize(const QImage& image) override
    {
        if (m_calls) {
            ++*m_calls;
        }
        OcrResult r;
        r.ok = !image.isNull();
        r.text = m_text;
        r.confidence = m_confidence;
        r.language = QStringLiteral("en-US");
        return r;
    }

private:
    QString m_text;
    double m_confidence;
    bool m_available;
    int* m_calls;
};

/** Text drawn into pixels only: what a scanner or a phone camera produces. */
QImage textImage(const QString& text, int width = 1600, int pointSize = 40)
{
    QImage image(width, 360, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    QFont font(QStringLiteral("Arial"));
    font.setPointSize(pointSize);
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(image.rect().adjusted(40, 40, -40, -40), Qt::TextWordWrap, text);
    p.end();
    return image;
}

/** A two-page PDF: page 1 is only a picture of text (a scan), page 2 has a real text layer. */
QString writeScannedPdf(const QString& dir)
{
    const QString path = dir + QStringLiteral("/scan.pdf");
    QPdfWriter writer(path);
    writer.setResolution(150);
    QPainter p(&writer);
    const QRect page = p.viewport();
    p.drawImage(QRect(page.left(), page.top(), page.width(), page.width() * 360 / 1600), textImage(kScannedText));
    writer.newPage();
    QFont font(QStringLiteral("Arial"));
    font.setPointSize(12);
    p.setFont(font);
    p.drawText(page.adjusted(20, 20, -20, -20), Qt::TextWordWrap,
               QStringLiteral("Cells store energy as ATP. Ribosomes build proteins from amino acids in every living cell."));
    p.end();
    return path;
}

QString writeFile(const QString& dir, const QString& name, const QByteArray& data)
{
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    return path;
}

QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class TestStudyOcr : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void scannedPdfPagesAreReadAndMarked();
    void lowConfidenceOrNoEngineLeavesPagesUnreadable();
    void imagesImportAsOnePageDocuments();
    void pictureOnlySlidesAreRead();
    void generatedSourcesSayScanned();
    void confidenceEstimateTellsTextFromNoise();
    void windowsOcrReadsRenderedText();

private:
    QTemporaryDir m_storage;
    QTemporaryDir m_inputs;
};

void TestStudyOcr::initTestCase()
{
    QVERIFY(m_storage.isValid());
    StudyDatabase::setStorageDirectory(m_storage.path());
}

void TestStudyOcr::cleanup()
{
    OcrEngine::setFactoryForTests({});
    QDir(m_storage.path()).removeRecursively();
}

void TestStudyOcr::scannedPdfPagesAreReadAndMarked()
{
    if (!DocumentExtractor::pdfSupported()) {
        QSKIP("PDF support not built");
    }
    int calls = 0;
    OcrEngine::setFactoryForTests([&calls]() { return std::make_unique<FakeOcrEngine>(kScannedText, 0.9, true, &calls); });
    const QString pdf = writeScannedPdf(m_inputs.path());
    const ExtractionResult raw = DocumentExtractor::extractPdf(pdf);
    QVERIFY(raw.ok);
    QCOMPARE(raw.unsupportedUnits, QList<int>{1});   // without OCR the scan has no text

    StudyLibrary library;
    QVERIFY(library.isOpen());
    const qint64 set = library.database()->addStudySet(QStringLiteral("Biology")).value_or(0);
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    QSignalSpy progress(&library, &StudyLibrary::documentProgress);
    const qint64 doc = library.importFiles(set, {pdf}).value(0);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 20000);
    QCOMPARE(calls, 1);   // only the scanned page is OCR'd

    const auto record = library.database()->document(doc);
    QCOMPARE(record->status, DocumentStatus::Extracted);
    QVERIFY(record->unsupportedUnits.isEmpty());
    const QHash<int, double> scanned = library.database()->ocrUnits(doc);
    QCOMPARE(scanned.keys(), QList<int>{1});
    QCOMPARE(scanned.value(1), 0.9);
    const QVector<Chunk> chunks = library.database()->chunks(doc);
    QVERIFY(chunks.size() >= 2);
    QCOMPARE(chunks.first().refNumber, 1);   // reading order is kept
    QVERIFY(chunks.first().text.contains(QStringLiteral("Mitochondria")));
    bool reported = false;
    for (const QList<QVariant>& args : std::as_const(progress)) {
        reported = reported || args.at(1).toString().contains(QStringLiteral("Reading scanned page 1 of 1"));
    }
    QVERIFY(reported);
    // The scanned page is also kept as an image to show with questions about it.
    const QVector<SupportAsset> assets = library.database()->supportAssets(doc, 1);
    QCOMPARE(assets.size(), 1);
    QVERIFY(QFileInfo::exists(assets.first().path));
}

void TestStudyOcr::lowConfidenceOrNoEngineLeavesPagesUnreadable()
{
    if (!DocumentExtractor::pdfSupported()) {
        QSKIP("PDF support not built");
    }
    const QString pdf = writeScannedPdf(m_inputs.path());
    {
        OcrEngine::setFactoryForTests([]() { return std::make_unique<FakeOcrEngine>(QStringLiteral("~~ ## ||| ~~ ## ||| ~~ ## |||"), 0.1, true, nullptr); });
        StudyLibrary library;
        const qint64 set = library.database()->addStudySet(QStringLiteral("Noise")).value_or(0);
        QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
        const qint64 doc = library.importFiles(set, {pdf}).value(0);
        QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 20000);
        const auto record = library.database()->document(doc);
        QCOMPARE(record->unsupportedUnits, QList<int>{1});
        QVERIFY(record->error.contains(QStringLiteral("even with text recognition")));
        QVERIFY(library.database()->ocrUnits(doc).isEmpty());
    }
    {
        OcrEngine::setFactoryForTests([]() { return std::make_unique<FakeOcrEngine>(kScannedText, 0.9, false, nullptr); });
        StudyLibrary library;
        const qint64 set = library.database()->addStudySet(QStringLiteral("No engine")).value_or(0);
        QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
        const qint64 doc = library.importFiles(set, {pdf}).value(0);
        QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 20000);
        const auto record = library.database()->document(doc);
        QCOMPARE(record->unsupportedUnits, QList<int>{1});
        QVERIFY(record->error.contains(QStringLiteral("text recognition is not available")));
    }
}

void TestStudyOcr::imagesImportAsOnePageDocuments()
{
    QCOMPARE(StudyText::kindForPath(QStringLiteral("notes/board.JPG")), DocumentKind::Image);
    QCOMPARE(StudyText::kindForPath(QStringLiteral("notes/board.png")), DocumentKind::Image);
    int calls = 0;
    OcrEngine::setFactoryForTests([&calls]() { return std::make_unique<FakeOcrEngine>(kScannedText, 0.85, true, &calls); });
    const QString photo = writeFile(m_inputs.path(), QStringLiteral("whiteboard.png"), png(textImage(kScannedText)));

    StudyLibrary library;
    const qint64 set = library.database()->addStudySet(QStringLiteral("Photos")).value_or(0);
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    const qint64 doc = library.importFiles(set, {photo}).value(0);
    QVERIFY(doc > 0);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 20000);
    const auto record = library.database()->document(doc);
    QCOMPARE(record->kind, DocumentKind::Image);
    QCOMPARE(record->status, DocumentStatus::Extracted);
    QCOMPARE(record->unitCount, 1);
    QCOMPARE(calls, 1);
    QVERIFY(library.database()->chunks(doc).first().text.contains(QStringLiteral("ATP")));
    const QVector<SupportAsset> assets = library.database()->supportAssets(doc);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().caption, QStringLiteral("whiteboard"));
    QVERIFY(assets.first().text.contains(QStringLiteral("Mitochondria")));   // the picture's own words
}

void TestStudyOcr::pictureOnlySlidesAreRead()
{
    OcrEngine::setFactoryForTests([]() { return std::make_unique<FakeOcrEngine>(kScannedText, 0.8, true, nullptr); });
    using StudyTestUtils::TestSlide;
    TestSlide picture;
    picture.picture = true;
    picture.pictureData = png(textImage(kScannedText, 800, 20));
    TestSlide text{QStringLiteral("Respiration"), {QStringLiteral("Cells release energy from glucose in their mitochondria.")}};
    const QString deck = writeFile(m_inputs.path(), QStringLiteral("cells.pptx"), StudyTestUtils::buildPptx({picture, text}));

    StudyLibrary library;
    const qint64 set = library.database()->addStudySet(QStringLiteral("Slides")).value_or(0);
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    const qint64 doc = library.importFiles(set, {deck}).value(0);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 20000);
    const auto record = library.database()->document(doc);
    QVERIFY(record->unsupportedUnits.isEmpty());
    QCOMPARE(library.database()->ocrUnits(doc).keys(), QList<int>{1});
    const QVector<Chunk> chunks = library.database()->chunks(doc);
    QCOMPARE(chunks.first().refKind, SegmentKind::Slide);
    QCOMPARE(chunks.first().refNumber, 1);
}

void TestStudyOcr::generatedSourcesSayScanned()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const qint64 set = db.addStudySet(QStringLiteral("Biology")).value_or(0);
    const qint64 doc = db.addDocument(set, QStringLiteral("Scan"), QStringLiteral("s.pdf"), DocumentKind::Pdf).value_or(0);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Page, 4, 0, kScannedText}}));
    db.setDocumentStatus(doc, DocumentStatus::Extracted);
    QVERIFY(db.setUnitOcr(doc, 4, 0.9));

    MockLlmClient llm;
    llm.responder = [](const LlmRequest&) -> MockLlmClient::Reply {
        QJsonArray options;
        const QStringList texts = {QStringLiteral("ATP"), QStringLiteral("DNA"), QStringLiteral("RNA"), QStringLiteral("Lipids")};
        for (int i = 0; i < 4; ++i) {
            options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)}, {QStringLiteral("correct"), i == 0},
                                       {QStringLiteral("why"), QStringLiteral("From your notes.")}});
        }
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("concept"), QStringLiteral("respiration product")},
            {QStringLiteral("evidence"), QStringLiteral("They turn glucose and oxygen into ATP")},
            {QStringLiteral("question"), QStringLiteral("What do mitochondria turn glucose and oxygen into?")},
            {QStringLiteral("options"), options},
            {QStringLiteral("explanation"), QStringLiteral("Cellular respiration makes ATP.")},
            {QStringLiteral("difficulty"), QStringLiteral("easy")},
        }).toJson(QJsonDocument::Compact));
    };
    QuestionGenerator gen(&db, &llm);
    QSignalSpy finished(&gen, &QuestionGenerator::finished);
    QVERIFY(gen.start({doc}, {QuestionType::SingleChoice}, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    const QVector<Question> saved = db.documentQuestions(doc);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().sources.first().location, QStringLiteral("p. 4 (scanned)"));
}

void TestStudyOcr::confidenceEstimateTellsTextFromNoise()
{
    QVERIFY(OcrEngine::estimateConfidence(kScannedText) > 0.8);
    QVERIFY(OcrEngine::estimateConfidence(QStringLiteral("~~ ## ||| ~~ ## |||")) < 0.2);
    QCOMPARE(OcrEngine::estimateConfidence(QString()), 0.0);
    QVERIFY(OcrEngine::estimateConfidence(QStringLiteral("Hi")) < OcrEngine::estimateConfidence(kScannedText));
}

void TestStudyOcr::windowsOcrReadsRenderedText()
{
    OcrEngine::setFactoryForTests({});
    const std::unique_ptr<OcrEngine> engine = OcrEngine::create();
    if (!engine->available()) {
        QSKIP("No OCR engine or OCR language on this system");
    }
    QVERIFY(!OcrEngine::statusText().isEmpty());
    const OcrResult result = engine->recognize(textImage(QStringLiteral("Mitochondria produce energy for the cell")));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.text.contains(QStringLiteral("Mitochondria"), Qt::CaseInsensitive), qPrintable(result.text));
    QVERIFY(!result.lines.isEmpty());
    QVERIFY(result.lines.first().box.width() > 0);
    QVERIFY(result.confidence > 0.5);
    // Works from the UI thread too (recognition runs on its own thread).
    QVERIFY(engine->recognize(QImage()).ok == false);
}

QTEST_MAIN(TestStudyOcr)
#include "test_study_ocr.moc"
