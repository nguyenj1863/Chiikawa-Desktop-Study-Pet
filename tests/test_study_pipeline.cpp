#include "StudyTestUtils.h"

#include "study/ingest/Chunker.h"
#include "study/Retriever.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace StudyTestUtils;

class TestStudyPipeline : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void chunkerKeepsReferencesAndBoundedSizes();
    void chunkerDropsRunningHeadersAndPageNumbers();
    void chunkerJoinsHyphenationAndSplitsLists();
    void databaseSetsAndDocuments();
    void ftsRetrievalRanksAndScopes();
    void removingDocumentOrSetCascades();
    void recoverInterruptedDocuments();

    void libraryImportsTextAndSlidesOnWorkerThread();
    void libraryFlagsUnsupportedDocuments();
    void libraryRemovesStoredCopyAndCancels();
    void libraryKeepsEventLoopResponsive();

private:
    QString writeFile(const QString& name, const QByteArray& data);
    QTemporaryDir m_storage;
    QTemporaryDir m_inputs;
};

void TestStudyPipeline::initTestCase()
{
    QVERIFY(m_storage.isValid());
    QVERIFY(m_inputs.isValid());
    StudyDatabase::setStorageDirectory(m_storage.path());
}

void TestStudyPipeline::cleanup()
{
    QDir(m_storage.path()).removeRecursively();
}

QString TestStudyPipeline::writeFile(const QString& name, const QByteArray& data)
{
    const QString path = m_inputs.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
    }
    return path;
}

void TestStudyPipeline::chunkerKeepsReferencesAndBoundedSizes()
{
    QString longPage;
    for (int i = 0; i < 40; ++i) {
        longPage += QStringLiteral("Sentence %1 explains how indexes speed up lookups in large tables. ").arg(i);
        if (i % 6 == 5) {
            longPage += QStringLiteral("\n\n");
        }
    }
    const QVector<Segment> segments = {
        {SegmentKind::Page, 1, QString(), longPage},
        {SegmentKind::Page, 2, QString(), QStringLiteral("Too short")},
        {SegmentKind::Slide, 3, QString(), QStringLiteral("A B-tree keeps keys sorted so range scans are efficient.")},
    };
    const QVector<Chunk> chunks = Chunker::chunk(segments);
    QVERIFY(chunks.size() >= 3);
    int ordinal = 0;
    for (const Chunk& c : chunks) {
        QVERIFY2(c.text.size() <= Chunker::kMaxChars, qPrintable(QString::number(c.text.size())));
        QCOMPARE(c.ordinal, ordinal++);
        QVERIFY(c.refNumber == 1 || c.refNumber == 3);   // page 2 is below the minimum
    }
    QCOMPARE(chunks.last().refKind, SegmentKind::Slide);
    QCOMPARE(chunks.last().refNumber, 3);
    // Every sentence of page 1 survives in exactly one chunk, in order.
    QString joined;
    for (const Chunk& c : chunks) {
        if (c.refNumber == 1) {
            joined += c.text + QLatin1Char(' ');
        }
    }
    for (int i = 0; i < 40; ++i) {
        QVERIFY(joined.contains(QStringLiteral("Sentence %1 explains").arg(i)));
    }
}

void TestStudyPipeline::chunkerDropsRunningHeadersAndPageNumbers()
{
    QVector<Segment> pages;
    for (int i = 1; i <= 5; ++i) {
        pages.append({SegmentKind::Page, i, QString(),
                      QStringLiteral("CS 340 Databases - Fall\nTopic %1 covers transactions and isolation levels in detail.\n%1 / 5").arg(i)});
    }
    const QStringList repeated = Chunker::repeatedLines(pages);
    QVERIFY(repeated.contains(QStringLiteral("CS 340 Databases - Fall")));
    const QVector<Chunk> chunks = Chunker::chunk(pages);
    QCOMPARE(chunks.size(), 5);
    for (const Chunk& c : chunks) {
        QVERIFY(!c.text.contains(QStringLiteral("CS 340")));
        QVERIFY(!c.text.contains(QStringLiteral("/ 5")));
        QVERIFY(c.text.startsWith(QStringLiteral("Topic")));
    }
    // Fewer than four pages: nothing is treated as a running header.
    QVERIFY(Chunker::repeatedLines(pages.mid(0, 3)).isEmpty());
}

void TestStudyPipeline::chunkerJoinsHyphenationAndSplitsLists()
{
    const QString cleaned = Chunker::cleanText(
        QStringLiteral("Normal-\nization removes redun-\n  dancy.\nIt continues on a wrapped line.\n- First item\n- Second item"));
    QVERIFY(cleaned.contains(QStringLiteral("Normalization removes redundancy.")));
    QVERIFY(cleaned.contains(QStringLiteral("redundancy. It continues on a wrapped line.")));
    QVERIFY(cleaned.contains(QStringLiteral("\n\n- First item\n\n- Second item")));
}

void TestStudyPipeline::databaseSetsAndDocuments()
{
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    QVERIFY(QFile::exists(StudyDatabase::databasePath()));

    const auto bio = db.addStudySet(QStringLiteral("  Biology  101 "));
    QVERIFY(bio);
    QVERIFY(!db.addStudySet(QStringLiteral("biology 101")).has_value());   // names are case-insensitive unique
    QVERIFY(!db.addStudySet(QStringLiteral("   ")).has_value());
    const auto cs = db.addStudySet(QStringLiteral("Algorithms"));
    QCOMPARE(db.studySets().size(), 2);
    QCOMPARE(db.studySets().first().name, QStringLiteral("Algorithms"));
    QVERIFY(db.renameStudySet(*cs, QStringLiteral("Data Structures")));
    QCOMPARE(db.studySets().last().name, QStringLiteral("Data Structures"));

    const auto doc = db.addDocument(*bio, QStringLiteral("Cells"), QStringLiteral("C:/notes/cells.pdf"), DocumentKind::Pdf);
    QVERIFY(doc);
    QVERIFY(db.setDocumentExtraction(*doc, 12, {4, 5, 9}));
    QVERIFY(db.setDocumentStatus(*doc, DocumentStatus::Extracted, QStringLiteral("note")));
    QVERIFY(db.setDocumentSelected(*doc, false));
    const auto rec = db.document(*doc);
    QVERIFY(rec);
    QCOMPARE(rec->title, QStringLiteral("Cells"));
    QCOMPARE(rec->kind, DocumentKind::Pdf);
    QCOMPARE(rec->status, DocumentStatus::Extracted);
    QCOMPARE(rec->unitCount, 12);
    QCOMPARE(rec->unsupportedUnits, (QList<int>{4, 5, 9}));
    QVERIFY(!rec->selected);
    QCOMPARE(db.documents(*bio).size(), 1);
    QCOMPARE(db.documents(*cs).size(), 0);
    QCOMPARE(db.documents(0).size(), 1);

    // A second connection (as a worker thread would use) sees the same data.
    StudyDatabase other;
    QVERIFY(other.open());
    QCOMPARE(other.documents(*bio).first().unitCount, 12);
}

void TestStudyPipeline::ftsRetrievalRanksAndScopes()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const auto studySet = *db.addStudySet(QStringLiteral("Databases"));
    const auto other = *db.addStudySet(QStringLiteral("Automata"));
    const auto doc1 = *db.addDocument(studySet, QStringLiteral("Joins"), QString(), DocumentKind::Pptx);
    const auto doc2 = *db.addDocument(studySet, QStringLiteral("Indexes"), QString(), DocumentKind::Pdf);
    const auto doc3 = *db.addDocument(other, QStringLiteral("NFA"), QString(), DocumentKind::Text);

    QVERIFY2(db.replaceChunks(doc1, {
        {0, 0, 0, SegmentKind::Slide, 4, 0, QStringLiteral("A LEFT JOIN returns all rows from the left table plus matching rows.")},
        {0, 0, 0, SegmentKind::Slide, 5, 1, QStringLiteral("An INNER JOIN returns only rows that match in both tables.")},
    }), qPrintable(db.lastError()));
    QVERIFY(db.replaceChunks(doc2, {
        {0, 0, 0, SegmentKind::Page, 2, 0, QStringLiteral("B-tree indexes speed up range queries on sorted keys.")},
    }));
    QVERIFY(db.replaceChunks(doc3, {
        {0, 0, 0, SegmentKind::Section, 1, 0, QStringLiteral("An NFA joins states through epsilon transitions.")},
    }));

    FtsRetriever retriever(&db);
    QVector<SearchHit> hits = retriever.search(QStringLiteral("What does a left join return?"), studySet, 5);
    QVERIFY(hits.size() >= 2);
    QCOMPARE(hits.first().chunk.refNumber, 4);   // stemming: "join" matches, "left" ranks slide 4 first
    QCOMPARE(hits.first().chunk.documentId, doc1);
    QVERIFY(hits.first().score >= hits.last().score);
    for (const SearchHit& h : hits) {
        QCOMPARE(h.chunk.setId, studySet);   // the Automata study set is never returned
    }

    hits = retriever.search(QStringLiteral("joins"), studySet, 5, {doc2});
    QVERIFY(hits.isEmpty());
    hits = retriever.search(QStringLiteral("range indexes"), studySet, 5, {doc2});
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().chunk.refKind, SegmentKind::Page);

    // Operator characters in user text never break the MATCH syntax.
    QVERIFY(retriever.search(QStringLiteral("\"left\" AND (join* OR NEAR) -- ;"), studySet, 5).size() >= 1);
    QVERIFY(retriever.search(QStringLiteral("the and of ?"), studySet, 5).isEmpty());
    QCOMPARE(FtsRetriever::matchExpression(QStringLiteral("Left JOIN left")), QStringLiteral("\"left\" OR \"join\""));

    // Replacing chunks keeps the FTS index in sync.
    QVERIFY(db.replaceChunks(doc2, {{0, 0, 0, SegmentKind::Page, 7, 0, QStringLiteral("Hash indexes support equality lookups.")}}));
    QVERIFY(retriever.search(QStringLiteral("range"), studySet, 5).isEmpty());
    QCOMPARE(retriever.search(QStringLiteral("equality"), studySet, 5).first().chunk.refNumber, 7);
}

void TestStudyPipeline::removingDocumentOrSetCascades()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const auto studySet = *db.addStudySet(QStringLiteral("Chemistry"));
    const auto doc = *db.addDocument(studySet, QStringLiteral("Bonds"), QString(), DocumentKind::Text);
    QVERIFY(db.replaceChunks(doc, {{0, 0, 0, SegmentKind::Section, 1, 0, QStringLiteral("Covalent bonds share electron pairs between atoms.")}}));
    {
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(QStringLiteral("INSERT INTO questions(id, document_id, set_id, type, prompt, payload, created_at)"
                                      " VALUES ('q1', %1, %2, 1, 'p', '{}', 0)").arg(doc).arg(studySet)));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO attempts(question_id, document_id, set_id, type, kind, verdict, score, answered_at)"
                                      " VALUES ('q1', %1, %2, 1, 0, 0, 1, 0)").arg(doc).arg(studySet)));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO concept_stats(document_id, set_id, concept) VALUES (%1, %2, 'bonds')").arg(doc).arg(studySet)));
    }
    FtsRetriever retriever(&db);
    QCOMPARE(retriever.search(QStringLiteral("covalent"), studySet, 5).size(), 1);

    QVERIFY(db.removeStudySet(studySet));
    QVERIFY(db.documents(studySet).isEmpty());
    QVERIFY(db.chunks(doc).isEmpty());
    QVERIFY(retriever.search(QStringLiteral("covalent"), studySet, 5).isEmpty());
    QSqlQuery q(db.connection());
    for (const char* table : {"questions", "attempts", "concept_stats", "chunks_fts"}) {
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table))));
        QVERIFY(q.next());
        QVERIFY2(q.value(0).toInt() == 0, table);
    }
}

void TestStudyPipeline::recoverInterruptedDocuments()
{
    StudyDatabase db;
    QVERIFY(db.open());
    const auto studySet = *db.addStudySet(QStringLiteral("History"));
    const auto extracting = *db.addDocument(studySet, QStringLiteral("a"), QString(), DocumentKind::Text);
    const auto generating = *db.addDocument(studySet, QStringLiteral("b"), QString(), DocumentKind::Text);
    db.setDocumentStatus(extracting, DocumentStatus::Extracting);
    db.setDocumentStatus(generating, DocumentStatus::Generating);
    db.recoverInterruptedDocuments();
    QCOMPARE(db.document(extracting)->status, DocumentStatus::Failed);
    QCOMPARE(db.document(generating)->status, DocumentStatus::Extracted);
}

void TestStudyPipeline::libraryImportsTextAndSlidesOnWorkerThread()
{
    StudyLibrary library;
    QVERIFY2(library.isOpen(), qPrintable(library.lastError()));
    const auto studySet = *library.database()->addStudySet(QStringLiteral("Databases"));
    const QString notes = writeFile(QStringLiteral("joins.md"),
                                    "# Joins\nA LEFT JOIN returns all rows from the left table.\n\n# Keys\nA primary key uniquely identifies a row.\n");
    const QString deck = writeFile(QStringLiteral("lecture.pptx"), buildPptx({
        {QStringLiteral("Transactions"), {QStringLiteral("ACID stands for atomicity, consistency, isolation, durability")}, QString(), false},
    }));
    const QString ignored = writeFile(QStringLiteral("talk.key"), "not a supported document");   // images are supported now

    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    QSignalSpy busy(&library, &StudyLibrary::busyChanged);
    const QList<qint64> ids = library.importFiles(studySet, {notes, deck, ignored});
    QCOMPARE(ids.size(), 2);
    QVERIFY(library.isBusy());
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!library.isBusy(), 2000);
    QVERIFY(!busy.isEmpty());
    QCOMPARE(busy.last().at(0).toBool(), false);

    for (const qint64 id : ids) {
        const auto doc = library.database()->document(id);
        QVERIFY(doc);
        QCOMPARE(doc->status, DocumentStatus::Extracted);
        QVERIFY(doc->chunkCount >= 1);
        QVERIFY(QFile::exists(doc->storedPath));
        QVERIFY(doc->storedPath.startsWith(StudyDatabase::filesDirectory()));
    }
    const auto deckDoc = library.database()->document(ids.at(1));
    QCOMPARE(deckDoc->title, QStringLiteral("lecture"));
    QCOMPARE(deckDoc->kind, DocumentKind::Pptx);
    QCOMPARE(deckDoc->unitCount, 1);
    const QVector<Chunk> chunks = library.database()->chunks(ids.at(1));
    QCOMPARE(chunks.first().refKind, SegmentKind::Slide);
    QCOMPARE(chunks.first().refNumber, 1);

    // The original can disappear; the library keeps working from its copy.
    QVERIFY(QFile::remove(deck));
    QVERIFY(library.reprocess(ids.at(1)));
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 3, 10000);
    QCOMPARE(library.database()->document(ids.at(1))->status, DocumentStatus::Extracted);
}

void TestStudyPipeline::libraryFlagsUnsupportedDocuments()
{
    StudyLibrary library;
    QVERIFY2(library.isOpen(), qPrintable(library.lastError()));
    const auto studySet = *library.database()->addStudySet(QStringLiteral("Art"));
    TestSlide picture;
    picture.picture = true;
    const QString deck = writeFile(QStringLiteral("paintings.pptx"), buildPptx({picture, picture}));
    const QString corrupt = writeFile(QStringLiteral("corrupt.pptx"), "PK this is not a real archive");
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    const QList<qint64> ids = library.importFiles(studySet, {deck, corrupt});
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 10000);

    const auto images = library.database()->document(ids.at(0));
    QCOMPARE(images->status, DocumentStatus::Unsupported);
    QCOMPARE(images->unsupportedUnits, (QList<int>{1, 2}));
    QVERIFY(images->error.contains(QStringLiteral("No readable text")));   // pictures with no image data to read
    QCOMPARE(images->chunkCount, 0);

    const auto bad = library.database()->document(ids.at(1));
    QCOMPARE(bad->status, DocumentStatus::Failed);
    QVERIFY(!bad->error.isEmpty());
}

void TestStudyPipeline::libraryRemovesStoredCopyAndCancels()
{
    StudyLibrary library;
    QVERIFY2(library.isOpen(), qPrintable(library.lastError()));
    const auto studySet = *library.database()->addStudySet(QStringLiteral("Physics"));
    const QString notes = writeFile(QStringLiteral("forces.txt"), "Newton's second law relates force, mass, and acceleration.\n");
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    const qint64 id = library.importFiles(studySet, {notes}).first();
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 10000);
    const QString stored = library.database()->document(id)->storedPath;
    QVERIFY(QFile::exists(stored));

    QVERIFY(library.removeDocument(id));
    QVERIFY(!library.database()->document(id));
    QVERIFY(!QFile::exists(stored));
    QVERIFY(library.database()->chunks(id).isEmpty());

    // Cancelling marks queued work as cancelled and leaves the library idle.
    QStringList many;
    for (int i = 0; i < 6; ++i) {
        many.append(writeFile(QStringLiteral("n%1.txt").arg(i), "Some study notes about momentum and energy conservation.\n"));
    }
    const QList<qint64> ids = library.importFiles(studySet, many);
    library.cancelAll();
    QTRY_VERIFY_WITH_TIMEOUT(!library.isBusy(), 10000);
    int cancelled = 0;
    for (const qint64 docId : ids) {
        const auto doc = library.database()->document(docId);
        QVERIFY(doc);
        if (doc->status == DocumentStatus::Failed && doc->error == QStringLiteral("Cancelled.")) {
            ++cancelled;
        } else {
            QCOMPARE(doc->status, DocumentStatus::Extracted);   // the one already running may finish
        }
    }
    QVERIFY(cancelled >= static_cast<int>(ids.size()) - 1);

    // Removing a whole study set deletes every stored copy.
    QVERIFY(library.removeStudySet(studySet));
    QTRY_VERIFY_WITH_TIMEOUT(QDir(StudyDatabase::filesDirectory()).entryList(QDir::Files).isEmpty(), 5000);
}

void TestStudyPipeline::libraryKeepsEventLoopResponsive()
{
    StudyLibrary library;
    QVERIFY2(library.isOpen(), qPrintable(library.lastError()));
    const auto studySet = *library.database()->addStudySet(QStringLiteral("Literature"));
    QByteArray big;
    for (int i = 0; i < 40000; ++i) {
        big += "Paragraph " + QByteArray::number(i) + " discusses themes of ambition and guilt in Macbeth.\n\n";
    }
    const QString path = writeFile(QStringLiteral("macbeth.txt"), big);

    int ticks = 0;
    QTimer ticker;
    connect(&ticker, &QTimer::timeout, this, [&ticks]() { ++ticks; });
    ticker.start(10);
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    QElapsedTimer clock;
    clock.start();
    library.importFiles(studySet, {path});
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 60000);
    const qint64 elapsed = clock.elapsed();
    ticker.stop();
    QVERIFY(library.database()->document(library.database()->documents(studySet).first().id)->chunkCount > 100);
    // The UI thread kept servicing timers while the worker parsed ~3 MB of text.
    if (elapsed > 200) {
        QVERIFY2(ticks >= elapsed / 10 / 4, qPrintable(QStringLiteral("ticks %1 in %2 ms").arg(ticks).arg(elapsed)));
    }
}

QTEST_MAIN(TestStudyPipeline)
#include "test_study_pipeline.moc"
