#include "study/StudyLibrary.h"

#include "study/Chunker.h"
#include "study/DocumentExtractor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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
    }
    emit documentsChanged();
    startNext();
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
    const ExtractionResult extraction = DocumentExtractor::extractFile(stored);
    if (cancelled()) {
        fail(QStringLiteral("Cancelled."));
        return;
    }
    if (!extraction.ok) {
        fail(extraction.error);
        return;
    }
    db.setDocumentExtraction(documentId, extraction.unitCount, extraction.unsupportedUnits);

    emit progress(documentId, QStringLiteral("Splitting into passages"));
    const QVector<Chunk> chunks = Chunker::chunk(extraction.segments);
    if (chunks.isEmpty()) {
        const QString message = extraction.unsupportedUnits.isEmpty()
            ? QStringLiteral("No readable text was found.")
            : QStringLiteral("No readable text was found. Scanned pages and image-only slides need OCR, which is not supported yet.");
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
        const QString unit = doc->kind == DocumentKind::Pptx ? QStringLiteral("slides") : QStringLiteral("pages");
        note = QStringLiteral("Skipped %1 %2 with no readable text (scans or images).")
                   .arg(unit, StudyText::compactRanges(extraction.unsupportedUnits));
    }
    db.setDocumentStatus(documentId, DocumentStatus::Extracted, note);
    emit finished(documentId, static_cast<int>(DocumentStatus::Extracted), note);
}
