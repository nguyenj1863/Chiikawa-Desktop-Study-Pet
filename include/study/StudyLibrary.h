#pragma once

#include "study/StudyDatabase.h"
#include "study/StudyTypes.h"

#include <QObject>
#include <QQueue>
#include <QThread>
#include <atomic>
#include <memory>

class IngestionWorker;

/**
 * UI-thread facade over the Study Library. Imports run one at a time on a
 * worker thread (copy into app data, extract, chunk, store) so parsing never
 * blocks the interface. Queued work can be cancelled.
 */
class StudyLibrary : public QObject {
    Q_OBJECT

public:
    explicit StudyLibrary(QObject* parent = nullptr);
    ~StudyLibrary() override;

    [[nodiscard]] bool isOpen() const { return m_db.isOpen(); }
    [[nodiscard]] QString lastError() const { return m_db.lastError(); }
    [[nodiscard]] StudyDatabase* database() { return &m_db; }

    /** Queues files for processing; returns the new document IDs (unsupported types are rejected). */
    QList<qint64> importFiles(qint64 courseId, const QStringList& paths);

    /** Re-runs extraction for a document whose stored copy still exists (or whose original path is reachable). */
    bool reprocess(qint64 documentId);

    /** Deletes a document, its stored copy, and all derived chunks, questions, and progress. */
    bool removeDocument(qint64 documentId);

    /** Deletes a course and everything in it. */
    bool removeCourse(qint64 courseId);

    /** Cancels the running import and clears the queue. */
    void cancelAll();

    [[nodiscard]] bool isBusy() const { return m_activeDocument != 0 || !m_queue.isEmpty(); }
    [[nodiscard]] qint64 activeDocument() const { return m_activeDocument; }

signals:
    void documentsChanged();
    void documentProgress(qint64 documentId, const QString& stage);
    void documentProcessed(qint64 documentId, DocumentStatus status, const QString& error);
    void busyChanged(bool busy);

private:
    void startNext();
    void onWorkerFinished(qint64 documentId, int status, const QString& error);

    StudyDatabase m_db;
    QThread m_thread;
    IngestionWorker* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QQueue<qint64> m_queue;
    qint64 m_activeDocument = 0;
};

/** Runs on the ingestion thread. Opens its own database connection per document. */
class IngestionWorker : public QObject {
    Q_OBJECT

public:
    explicit IngestionWorker(std::shared_ptr<std::atomic_bool> cancel) : m_cancel(std::move(cancel)) {}

public slots:
    void process(qint64 documentId);

signals:
    void progress(qint64 documentId, const QString& stage);
    void finished(qint64 documentId, int status, const QString& error);

private:
    [[nodiscard]] bool cancelled() const { return m_cancel && m_cancel->load(); }

    std::shared_ptr<std::atomic_bool> m_cancel;
};
