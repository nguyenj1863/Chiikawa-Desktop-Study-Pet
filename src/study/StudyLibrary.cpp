#include "study/StudyLibrary.h"

#include "study/ingest/Chunker.h"
#include "study/ingest/DocumentExtractor.h"
#include "study/ingest/OcrEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

StudyLibrary::StudyLibrary(QObject* parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic_bool>(false))
{
    qRegisterMetaType<DocumentStatus>();
    if (m_db.open()) {
        m_db.recoverInterruptedDocuments();
    }
    m_worker = new IngestionWorker(m_cancel);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &IngestionWorker::progress, this, &StudyLibrary::documentProgress);
    connect(m_worker, &IngestionWorker::finished, this, &StudyLibrary::onWorkerFinished);
    m_thread.setObjectName(QStringLiteral("StudyIngestion"));
    m_thread.start(QThread::LowPriority);
}

StudyLibrary::~StudyLibrary()
{
    m_cancel->store(true);
    m_queue.clear();
    m_thread.quit();
    m_thread.wait();
}

QList<qint64> StudyLibrary::importFiles(qint64 setId, const QStringList& paths)
{
    QList<qint64> ids;
    if (!m_db.isOpen() || setId <= 0) {
        return ids;
    }
    for (const QString& path : paths) {
        const DocumentKind kind = StudyText::kindForPath(path);
        if (kind == DocumentKind::Unknown) {
            continue;
        }
        const auto id = m_db.addDocument(setId, QFileInfo(path).completeBaseName(), path, kind);
        if (id) {
            ids.append(*id);
            m_queue.enqueue(*id);
        }
    }
    if (!ids.isEmpty()) {
        emit documentsChanged();
        startNext();
    }
    return ids;
}

bool StudyLibrary::reprocess(qint64 documentId)
{
    const auto doc = m_db.document(documentId);
    if (!doc || documentId == m_activeDocument || m_queue.contains(documentId)) {
        return false;
    }
    m_db.setDocumentStatus(documentId, DocumentStatus::Queued);
    m_queue.enqueue(documentId);
    emit documentsChanged();
    startNext();
    return true;
}

bool StudyLibrary::removeDocument(qint64 documentId)
{
    const auto doc = m_db.document(documentId);
    if (!doc) {
        return false;
    }
    m_queue.removeAll(documentId);
    if (documentId == m_activeDocument) {
        // The worker notices on its next step; its result for this ID is ignored.
        m_cancel->store(true);
    }
    const bool ok = m_db.removeDocument(documentId);
    if (!doc->storedPath.isEmpty()) {
        QFile::remove(doc->storedPath);
    }
    QDir(StudyDatabase::assetsDirectory(documentId)).removeRecursively();
    emit documentsChanged();
    return ok;
}

bool StudyLibrary::removeStudySet(qint64 setId)
{
    const QVector<DocumentRecord> docs = m_db.documents(setId);
    for (const DocumentRecord& d : docs) {
        m_queue.removeAll(d.id);
        if (d.id == m_activeDocument) {
            m_cancel->store(true);
        }
        if (!d.storedPath.isEmpty()) {
            QFile::remove(d.storedPath);
        }
        QDir(StudyDatabase::assetsDirectory(d.id)).removeRecursively();
    }
    const bool ok = m_db.removeStudySet(setId);
    emit documentsChanged();
    return ok;
}

void StudyLibrary::cancelAll()
{
    for (const qint64 id : std::as_const(m_queue)) {
        m_db.setDocumentStatus(id, DocumentStatus::Failed, QStringLiteral("Cancelled."));
    }
    m_queue.clear();
    if (m_activeDocument != 0) {
        m_cancel->store(true);
    }
    emit documentsChanged();
    if (!isBusy()) {
        emit busyChanged(false);
    }
}

void StudyLibrary::startNext()
{
    if (m_activeDocument != 0) {
        return;
    }
    if (m_queue.isEmpty()) {
        emit busyChanged(false);
        return;
    }
    m_activeDocument = m_queue.dequeue();
    m_cancel->store(false);
    emit busyChanged(true);
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection, Q_ARG(qint64, m_activeDocument));
}

void StudyLibrary::onWorkerFinished(qint64 documentId, int status, const QString& error)
{
    if (documentId == m_activeDocument) {
        m_activeDocument = 0;
    }
    const bool stillExists = m_db.document(documentId).has_value();
    if (stillExists) {
        emit documentProcessed(documentId, static_cast<DocumentStatus>(status), error);
    } else {
        // Removed while processing: drop any copy the worker made after the removal.
        const QDir files(StudyDatabase::filesDirectory());
        for (const QString& name : files.entryList({QStringLiteral("%1.*").arg(documentId)}, QDir::Files)) {
            QFile::remove(files.filePath(name));
        }
        QDir(StudyDatabase::assetsDirectory(documentId)).removeRecursively();
    }
    emit documentsChanged();
    startNext();
}

bool IngestionWorker::readScannedUnits(StudyDatabase& db, qint64 documentId, DocumentKind kind, const QString& stored,
                                       ExtractionResult& extraction)
{
    const std::unique_ptr<OcrEngine> engine = OcrEngine::create();
    if (!engine->available()) {
        return false;
    }
    const QString unitName = kind == DocumentKind::Pptx ? QStringLiteral("slide") : QStringLiteral("page");
    const QList<int> units = extraction.unsupportedUnits;
    QList<int> stillUnreadable;
    for (qsizetype i = 0; i < units.size(); ++i) {
        const int unit = units.at(i);
        if (cancelled()) {
            stillUnreadable += units.mid(i);
            break;
        }
        emit progress(documentId, QStringLiteral("Reading scanned %1 %2 of %3").arg(unitName).arg(i + 1).arg(units.size()));
        // PDF pages are rendered sharply for OCR; slides and images use their own pictures.
        QList<QPair<QImage, ExtractedAsset*>> images;
        if (kind == DocumentKind::Pdf) {
            images.append({DocumentExtractor::renderPdfPageImage(stored, unit, kOcrLongEdge), nullptr});
        }
        for (ExtractedAsset& asset : extraction.assets) {
            if (asset.unitNumber == unit && asset.kind == SupportKind::Image && kind != DocumentKind::Pdf) {
                images.append({QImage::fromData(asset.imageData), &asset});
            }
        }
        QStringList texts;
        double confidence = 0.0;
        int read = 0;
        for (const auto& [image, asset] : images) {
            const OcrResult ocr = engine->recognize(image);
            if (!ocr.ok || ocr.text.trimmed().isEmpty()) {
                continue;
            }
            texts.append(ocr.text);
            confidence += ocr.confidence;
            ++read;
            if (asset) {
                asset->text = ocr.text;   // the figure's own words, for questions about it
            }
        }
        const QString text = texts.join(QStringLiteral("\n\n"));
        const double average = read > 0 ? confidence / read : 0.0;
        int visible = 0;
        for (const QChar ch : text) {
            visible += ch.isSpace() ? 0 : 1;
        }
        if (visible < DocumentExtractor::kMinPageTextChars || average < kMinOcrConfidence) {
            stillUnreadable.append(unit);
            continue;
        }
        Segment segment;
        segment.kind = kind == DocumentKind::Pptx ? SegmentKind::Slide : SegmentKind::Page;
        segment.number = unit;
        segment.text = text;
        segment.ocr = true;
        extraction.segments.append(segment);
        db.setUnitOcr(documentId, unit, average);
    }
    // Keep reading order: OCR'd pages go back between the pages around them.
    std::stable_sort(extraction.segments.begin(), extraction.segments.end(),
                     [](const Segment& a, const Segment& b) { return a.number < b.number; });
    extraction.unsupportedUnits = stillUnreadable;
    return true;
}

bool IngestionWorker::storeAssets(StudyDatabase& db, qint64 documentId, const QVector<ExtractedAsset>& assets)
{
    const QString dir = StudyDatabase::assetsDirectory(documentId);
    if (!QDir().mkpath(dir)) {
        return false;
    }
    int index = 0;
    for (const ExtractedAsset& e : assets) {
        if (cancelled()) {
            return true;
        }
        SupportAsset a;
        a.documentId = documentId;
        a.unitKind = e.unitKind;
        a.unitNumber = e.unitNumber;
        a.kind = e.kind;
        a.caption = e.caption;
        a.table = e.table;
        a.text = e.text;
        if (e.kind == SupportKind::Image) {
            a.path = QDir(dir).filePath(QStringLiteral("%1-%2.%3").arg(e.unitNumber).arg(++index).arg(e.imageFormat));
            QFile file(a.path);
            if (!file.open(QIODevice::WriteOnly) || file.write(e.imageData) != e.imageData.size()) {
                return false;
            }
            file.close();
            const QSize size = QImageReader(a.path).size();
            a.width = size.width();
            a.height = size.height();
        }
        if (!db.addSupportAsset(a)) {
            return false;
        }
    }
    return true;
}

void IngestionWorker::process(qint64 documentId)
{
    StudyDatabase db;
    if (!db.open()) {
        emit finished(documentId, static_cast<int>(DocumentStatus::Failed), db.lastError());
        return;
    }
    const auto doc = db.document(documentId);
    if (!doc) {
        emit finished(documentId, static_cast<int>(DocumentStatus::Failed), QStringLiteral("Document was removed."));
        return;
    }
    const auto fail = [&](const QString& message) {
        db.setDocumentStatus(documentId, DocumentStatus::Failed, message);
        emit finished(documentId, static_cast<int>(DocumentStatus::Failed), message);
    };

    db.setDocumentStatus(documentId, DocumentStatus::Extracting);
    emit progress(documentId, QStringLiteral("Copying"));

    // Keep a private copy so the library survives the original file moving.
    QString stored = doc->storedPath;
    if (stored.isEmpty() || !QFile::exists(stored)) {
        const QString suffix = QFileInfo(doc->originalPath).suffix().toLower();
        stored = QDir(StudyDatabase::filesDirectory()).filePath(QStringLiteral("%1.%2").arg(documentId).arg(suffix));
        QFile::remove(stored);
        if (!QFile::copy(doc->originalPath, stored)) {
            fail(QStringLiteral("The file could not be copied into the library."));
            return;
        }
        QFile::setPermissions(stored, QFile::ReadOwner | QFile::WriteOwner);
        db.setDocumentStoredPath(documentId, stored);
    }
    if (cancelled()) {
        fail(QStringLiteral("Cancelled."));
        return;
    }

    emit progress(documentId, QStringLiteral("Reading text"));
    ExtractionResult extraction = DocumentExtractor::extractFile(stored);
    if (cancelled()) {
        fail(QStringLiteral("Cancelled."));
        return;
    }
    if (!extraction.ok) {
        fail(extraction.error);
        return;
    }
    // Scanned pages, picture-only slides and images: read their text with the system's OCR.
    db.clearOcrUnits(documentId);
    bool ocrAvailable = true;
    if (!extraction.unsupportedUnits.isEmpty()) {
        ocrAvailable = readScannedUnits(db, documentId, doc->kind, stored, extraction);
        if (cancelled()) {
            fail(QStringLiteral("Cancelled."));
            return;
        }
    }
    db.setDocumentExtraction(documentId, extraction.unitCount, extraction.unsupportedUnits);

    // Figures and tables are kept so questions about them can show them.
    db.removeSupportAssets(documentId);
    if (doc->kind == DocumentKind::Image) {
        for (ExtractedAsset& asset : extraction.assets) {
            asset.caption = doc->title;
        }
    }
    if (!extraction.assets.isEmpty()) {
        emit progress(documentId, QStringLiteral("Saving figures and tables"));
        if (!storeAssets(db, documentId, extraction.assets)) {
            fail(QStringLiteral("Figures could not be saved: %1").arg(db.lastError()));
            return;
        }
    }

    emit progress(documentId, QStringLiteral("Splitting into passages"));
    const QVector<Chunk> chunks = Chunker::chunk(extraction.segments);
    if (chunks.isEmpty()) {
        const QString message = extraction.unsupportedUnits.isEmpty() ? QStringLiteral("No readable text was found.")
            : ocrAvailable ? QStringLiteral("No readable text was found, even with text recognition.")
                           : QStringLiteral("No readable text was found. %1").arg(OcrEngine::statusText());
        db.replaceChunks(documentId, {});
        db.setDocumentStatus(documentId, DocumentStatus::Unsupported, message);
        emit finished(documentId, static_cast<int>(DocumentStatus::Unsupported), message);
        return;
    }
    if (!db.replaceChunks(documentId, chunks)) {
        fail(db.lastError());
        return;
    }
    if (cancelled()) {
        fail(QStringLiteral("Cancelled."));
        return;
    }
    QString note;
    if (!extraction.unsupportedUnits.isEmpty()) {
        const bool one = extraction.unsupportedUnits.size() == 1;
        const QString unit = doc->kind == DocumentKind::Pptx ? (one ? QStringLiteral("slide") : QStringLiteral("slides"))
                                                             : (one ? QStringLiteral("page") : QStringLiteral("pages"));
        note = QStringLiteral("Skipped %1 %2 with no readable text%3.")
                   .arg(unit, StudyText::compactRanges(extraction.unsupportedUnits),
                        ocrAvailable ? QStringLiteral(", even with text recognition") : QStringLiteral(" (text recognition is not available)"));
    }
    db.setDocumentStatus(documentId, DocumentStatus::Extracted, note);
    emit finished(documentId, static_cast<int>(DocumentStatus::Extracted), note);
}
