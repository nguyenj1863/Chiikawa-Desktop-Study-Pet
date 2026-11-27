#include "StudyTestUtils.h"

#include "study/DocumentExtractor.h"
#include "study/ZipReader.h"

#include <QFile>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace StudyTestUtils;

class TestStudyExtract : public QObject {
    Q_OBJECT

private slots:
    void inflateMatchesZlibAcrossInputsAndLevels();
    void inflateRejectsTruncatedAndOversizedData();
    void zipReadsStoredAndDeflateEntries();
    void zipRejectsCorruptionAndNonArchives();
    void pptxUsesPresentationOrderNotesAndSkipsPlaceholders();
    void pptxFlagsPictureOnlySlides();
    void pptxRejectsOtherZipFiles();
    void markdownSplitsByHeadings();
    void plainTextGroupsParagraphsAndHandlesEncodings();
    void pdfExtractsPagesAndFlagsScannedPages();
    void extractFileDispatchesAndReportsErrors();
    void referenceLabelsAndRanges();
};

void TestStudyExtract::inflateMatchesZlibAcrossInputsAndLevels()
{
    QRandomGenerator rng(42);
    QList<QByteArray> inputs = {
        QByteArray("a"),
        QByteArray("Hello, hello, hello! Repetition makes back-references."),
        QByteArray(70000, 'x'),   // long runs crossing the 32 KiB window
    };
    QByteArray text;
    for (int i = 0; i < 3000; ++i) {
        text += "The left join keeps every row of the left table. ";
        text += QByteArray::number(i);
    }
    inputs.append(text);
    QByteArray noise(50000, '\0');
    for (char& c : noise) {
        c = static_cast<char>(rng.bounded(256));
    }
    inputs.append(noise);   // incompressible data exercises stored blocks

    for (const QByteArray& input : inputs) {
        for (const int level : {0, 1, 6, 9}) {
            const auto out = ZipReader::inflateRaw(rawDeflate(input, level), input.size());
            QVERIFY2(out.has_value(), qPrintable(QStringLiteral("size %1 level %2").arg(input.size()).arg(level)));
            QCOMPARE(*out, input);
        }
    }
}

void TestStudyExtract::inflateRejectsTruncatedAndOversizedData()
{
    const QByteArray input(5000, 'q');
    const QByteArray deflated = rawDeflate(QByteArray("abcdefghij").repeated(400) + input);
    QVERIFY(!ZipReader::inflateRaw(deflated.left(deflated.size() / 2), -1).has_value());
    // Output larger than the declared size is refused (zip-bomb guard).
    QVERIFY(!ZipReader::inflateRaw(deflated, 100).has_value());
    QVERIFY(!ZipReader::inflateRaw(QByteArray("\xff\xff\xff\xff", 4), -1).has_value());
}

void TestStudyExtract::zipReadsStoredAndDeflateEntries()
{
    ZipWriter writer;
    writer.add(QStringLiteral("stored.txt"), QByteArray("plain stored bytes"), false);
    writer.add(QStringLiteral("dir/deflated.xml"), QByteArray("<x>").repeated(500), true);
    writer.add(QStringLiteral("empty"), QByteArray(), false);
    ZipReader zip;
    QVERIFY(zip.open(writer.finish()));
    QCOMPARE(zip.entryNames(), (QStringList{QStringLiteral("stored.txt"), QStringLiteral("dir/deflated.xml"), QStringLiteral("empty")}));
    QCOMPARE(*zip.read(QStringLiteral("stored.txt")), QByteArray("plain stored bytes"));
    QCOMPARE(*zip.read(QStringLiteral("dir/deflated.xml")), QByteArray("<x>").repeated(500));
    QCOMPARE(*zip.read(QStringLiteral("empty")), QByteArray());
    QVERIFY(!zip.read(QStringLiteral("missing")).has_value());
}

void TestStudyExtract::zipRejectsCorruptionAndNonArchives()
{
    ZipWriter writer;
    writer.add(QStringLiteral("a.txt"), QByteArray("content that will fail its checksum"));
    writer.breakCrc(QStringLiteral("a.txt"));
    ZipReader zip;
    QVERIFY(zip.open(writer.finish()));
    QVERIFY(!zip.read(QStringLiteral("a.txt")).has_value());

    ZipReader notZip;
    QVERIFY(!notZip.open(QByteArray("%PDF-1.4 this is not a zip file at all, just text")));
    QVERIFY(!notZip.open(QByteArray()));
    QVERIFY(!notZip.isOpen());
}

void TestStudyExtract::pptxUsesPresentationOrderNotesAndSkipsPlaceholders()
{
    const QByteArray pptx = buildPptx({
        {QStringLiteral("SQL Joins"), {QStringLiteral("INNER JOIN returns matching rows only"),
                                       QStringLiteral("LEFT JOIN keeps every left-table row")},
         QStringLiteral("Mention NULLs for unmatched right rows."), false},
        {QStringLiteral("Finite Automata"), {QStringLiteral("An NFA may use epsilon transitions")}, QString(), false},
        {QStringLiteral("Summary & Review"), {QStringLiteral("Practice <joins> daily")}, QString(), false},
    });
    const ExtractionResult r = DocumentExtractor::extractPptx(pptx);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.kind, DocumentKind::Pptx);
    QCOMPARE(r.unitCount, 3);
    QVERIFY(r.unsupportedUnits.isEmpty());
    QCOMPARE(r.segments.size(), 4);

    QCOMPARE(r.segments.at(0).kind, SegmentKind::Slide);
    QCOMPARE(r.segments.at(0).number, 1);
    QVERIFY(r.segments.at(0).text.startsWith(QStringLiteral("SQL Joins")));
    QVERIFY(r.segments.at(0).text.contains(QStringLiteral("LEFT JOIN keeps every left-table row")));
    QVERIFY(!r.segments.at(0).text.contains(QStringLiteral("99")));   // slide-number placeholder dropped

    QCOMPARE(r.segments.at(1).kind, SegmentKind::SlideNotes);
    QCOMPARE(r.segments.at(1).number, 1);
    QCOMPARE(r.segments.at(1).text, QStringLiteral("Mention NULLs for unmatched right rows."));

    QCOMPARE(r.segments.at(2).number, 2);
    QVERIFY(r.segments.at(2).text.contains(QStringLiteral("epsilon")));
    QCOMPARE(r.segments.at(3).number, 3);
    QVERIFY(r.segments.at(3).text.contains(QStringLiteral("Summary & Review")));
    QVERIFY(r.segments.at(3).text.contains(QStringLiteral("Practice <joins> daily")));
}

void TestStudyExtract::pptxFlagsPictureOnlySlides()
{
    TestSlide picture;
    picture.picture = true;
    picture.slideNumberText = QStringLiteral("2");
    TestSlide blank;
    const ExtractionResult r = DocumentExtractor::extractPptx(buildPptx({
        {QStringLiteral("Intro"), {QStringLiteral("Some readable text on the first slide")}, QString(), false},
        picture,
        blank,
    }));
    QVERIFY(r.ok);
    QCOMPARE(r.unitCount, 3);
    QCOMPARE(r.unsupportedUnits, QList<int>{2});   // the blank slide is skipped, not flagged
    QCOMPARE(r.segments.size(), 1);
}

void TestStudyExtract::pptxRejectsOtherZipFiles()
{
    ZipWriter writer;
    writer.add(QStringLiteral("word/document.xml"), QByteArray("<w:document/>"));
    const ExtractionResult r = DocumentExtractor::extractPptx(writer.finish());
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(!DocumentExtractor::extractPptx(QByteArray("garbage")).ok);
}

void TestStudyExtract::markdownSplitsByHeadings()
{
    const QByteArray md =
        "Intro paragraph before any heading.\n\n"
        "# Normalization\n"
        "First normal form removes repeating groups.\n\n"
        "## Second Normal Form ##\n"
        "2NF removes partial dependencies on a composite key.\n"
        "   \n"
        "#NotAHeading because there is no space\n";
    const ExtractionResult r = DocumentExtractor::extractText(md, true);
    QVERIFY(r.ok);
    QCOMPARE(r.segments.size(), 3);
    QCOMPARE(r.segments.at(0).heading, QString());
    QCOMPARE(r.segments.at(1).heading, QStringLiteral("Normalization"));
    QCOMPARE(r.segments.at(1).number, 2);
    QCOMPARE(r.segments.at(2).heading, QStringLiteral("Second Normal Form"));
    QVERIFY(r.segments.at(2).text.contains(QStringLiteral("#NotAHeading")));
    QCOMPARE(r.unitCount, 3);
}

void TestStudyExtract::plainTextGroupsParagraphsAndHandlesEncodings()
{
    QByteArray txt = "\xEF\xBB\xBF";   // UTF-8 BOM
    for (int i = 1; i <= 20; ++i) {
        txt += "Paragraph " + QByteArray::number(i) + " about caf\xC3\xA9 culture.\r\n\r\n";
    }
    const ExtractionResult r = DocumentExtractor::extractText(txt, false);
    QVERIFY(r.ok);
    QCOMPARE(r.segments.size(), 3);   // groups of 8 paragraphs
    QVERIFY(r.segments.at(0).text.startsWith(QStringLiteral("Paragraph 1 about café")));
    QVERIFY(!r.segments.at(0).text.contains(QLatin1Char('\r')));

    // Invalid UTF-8 falls back to the local 8-bit codec instead of failing.
    const ExtractionResult latin = DocumentExtractor::extractText(QByteArray("Na\xEFve text with a stray byte."), false);
    QVERIFY(latin.ok);
    QVERIFY(latin.segments.first().text.startsWith(QStringLiteral("Na")));

    QVERIFY(!DocumentExtractor::extractText(QByteArray(" \n\n \t"), false).ok);
}

void TestStudyExtract::pdfExtractsPagesAndFlagsScannedPages()
{
    if (!DocumentExtractor::pdfSupported()) {
        QSKIP("Built without QtPdf; PDF extraction is unavailable in this build.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("lecture.pdf"));
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter p(&writer);
        QFont font(QStringLiteral("Arial"));
        font.setPointSize(12);
        p.setFont(font);
        p.drawText(QPointF(50, 80), QStringLiteral("Relational algebra defines selection and projection."));
        p.drawText(QPointF(50, 110), QStringLiteral("A LEFT JOIN keeps every row from the left table."));
        writer.newPage();
        // A "scanned" page: only an image, no text layer.
        QImage scan(300, 200, QImage::Format_RGB32);
        scan.fill(Qt::lightGray);
        p.drawImage(QPointF(50, 50), scan);
        writer.newPage();
        p.drawText(QPointF(50, 80), QStringLiteral("Third page: normalization reduces redundancy in tables."));
        p.end();
    }
    const ExtractionResult r = DocumentExtractor::extractFile(path);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.kind, DocumentKind::Pdf);
    QCOMPARE(r.unitCount, 3);
    QCOMPARE(r.unsupportedUnits, QList<int>{2});
    QCOMPARE(r.segments.size(), 2);
    QCOMPARE(r.segments.at(0).number, 1);
    QVERIFY2(r.segments.at(0).text.contains(QStringLiteral("LEFT JOIN")), qPrintable(r.segments.at(0).text));
    QCOMPARE(r.segments.at(1).number, 3);
    QVERIFY(r.segments.at(1).text.contains(QStringLiteral("normalization")));

    // Corrupt PDFs produce a readable error instead of crashing.
    const QString broken = dir.filePath(QStringLiteral("broken.pdf"));
    QFile f(broken);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.7\nthis is not really a pdf");
    f.close();
    const ExtractionResult bad = DocumentExtractor::extractFile(broken);
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());
}

void TestStudyExtract::extractFileDispatchesAndReportsErrors()
{
    QTemporaryDir dir;
    const QString pptxPath = dir.filePath(QStringLiteral("deck.PPTX"));
    QFile f(pptxPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(buildPptx({{QStringLiteral("Title"), {QStringLiteral("Body text for the only slide")}, QString(), false}}));
    f.close();
    QVERIFY(DocumentExtractor::extractFile(pptxPath).ok);

    const QString docx = dir.filePath(QStringLiteral("essay.docx"));
    QFile d(docx);
    QVERIFY(d.open(QIODevice::WriteOnly));
    d.write("x");
    d.close();
    const ExtractionResult unsupported = DocumentExtractor::extractFile(docx);
    QVERIFY(!unsupported.ok);
    QVERIFY(unsupported.error.contains(QStringLiteral("PDF, PPTX, TXT, or MD")));

    QVERIFY(!DocumentExtractor::extractFile(dir.filePath(QStringLiteral("missing.pdf"))).ok);
}

void TestStudyExtract::referenceLabelsAndRanges()
{
    QCOMPARE(StudyText::referenceLabel(SegmentKind::Page, 5), QStringLiteral("p. 5"));
    QCOMPARE(StudyText::referenceLabel(SegmentKind::Slide, 3), QStringLiteral("slide 3"));
    QCOMPARE(StudyText::referenceLabel(SegmentKind::SlideNotes, 3), QStringLiteral("slide 3 notes"));
    QCOMPARE(StudyText::compactRanges({7, 1, 2, 3, 3, 9, 10}), QStringLiteral("1-3, 7, 9-10"));
    QCOMPARE(StudyText::kindForPath(QStringLiteral("x/Notes.MD")), DocumentKind::Text);
    QCOMPARE(StudyText::kindForPath(QStringLiteral("x/y.key")), DocumentKind::Unknown);
}

QTEST_MAIN(TestStudyExtract)
#include "test_study_extract.moc"
