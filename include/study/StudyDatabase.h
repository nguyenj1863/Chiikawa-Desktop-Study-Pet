#pragma once

#include "study/StudyTypes.h"

#include <QSqlDatabase>
#include <QString>
#include <optional>

/**
 * Local SQLite store for the Study Library: courses, documents, chunks (with an
 * FTS5 index), generated questions, attempts, and per-concept progress.
 *
 * Each instance owns its own named connection and must be used only on the thread
 * that created it. Worker threads create their own instance on the same file;
 * WAL mode and a busy timeout let them coexist with the UI-thread instance.
 */
class StudyDatabase {
public:
    static constexpr int kSchemaVersion = 1;

    StudyDatabase();
    ~StudyDatabase();
    StudyDatabase(const StudyDatabase&) = delete;
    StudyDatabase& operator=(const StudyDatabase&) = delete;

    /**
     * Overrides the storage directory (tests). Empty restores the default
     * AppDataLocation/study. Must be called before any instance opens.
     */
    static void setStorageDirectory(const QString& dir);
    [[nodiscard]] static QString storageDirectory();
    [[nodiscard]] static QString databasePath();
    [[nodiscard]] static QString filesDirectory();

    /** Opens (creating and migrating if needed). Safe to call repeatedly. */
    bool open();
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QSqlDatabase connection() const;

    // Courses
    [[nodiscard]] std::optional<qint64> addCourse(const QString& name);
    bool renameCourse(qint64 id, const QString& name);
    /** Deletes the course and, by cascade, its documents, chunks, questions, attempts, and stats. */
    bool removeCourse(qint64 id);
    [[nodiscard]] QVector<Course> courses();

    // Documents
    [[nodiscard]] std::optional<qint64> addDocument(qint64 courseId, const QString& title,
                                                    const QString& originalPath, DocumentKind kind);
    bool setDocumentStoredPath(qint64 id, const QString& storedPath);
    bool setDocumentStatus(qint64 id, DocumentStatus status, const QString& error = QString());
    bool setDocumentExtraction(qint64 id, int unitCount, const QList<int>& unsupportedUnits);
    bool setDocumentSelected(qint64 id, bool selected);
    [[nodiscard]] std::optional<DocumentRecord> document(qint64 id);
    /** Documents in a course with chunk and question counts; all courses when courseId is 0. */
    [[nodiscard]] QVector<DocumentRecord> documents(qint64 courseId);
    bool removeDocument(qint64 id);

    // Chunks
    /** Replaces all chunks of a document in one transaction (FTS index kept in sync by triggers). */
    bool replaceChunks(qint64 documentId, const QVector<Chunk>& chunks);
    [[nodiscard]] QVector<Chunk> chunks(qint64 documentId);
    [[nodiscard]] std::optional<Chunk> chunk(qint64 chunkId);

    /** Marks documents left mid-processing by a previous run as failed so they can be retried. */
    void recoverInterruptedDocuments();

private:
    bool exec(const QString& sql);
    bool migrate();

    QString m_connectionName;
    QString m_lastError;
};
