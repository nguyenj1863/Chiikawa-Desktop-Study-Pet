#pragma once

#include "study/StudyTypes.h"
#include "study/Question.h"

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <optional>

/**
 * Local SQLite store for the Study Library: studySets, documents, chunks (with an
 * FTS5 index), generated questions, attempts, and per-concept progress.
 *
 * Each instance owns its own named connection and must be used only on the thread
 * that created it. Worker threads create their own instance on the same file;
 * WAL mode and a busy timeout let them coexist with the UI-thread instance.
 */
class StudyDatabase {
public:
    /**
     * 1: original Study Library tables (courses). 2: courses become study sets with optional
     * parent sets, and review scheduling, practice sessions, and saved filters are added.
     * 3: XP is removed from attempts, and questions record where they came from (origin).
     * 4: figures and tables kept beside questions (support_assets), and pages read by OCR (document_units).
     */
    static constexpr int kSchemaVersion = 4;
    /** Copies of the database made before upgrading from version 1, 2 or 3, kept next to library.db. */
    static constexpr auto kVersion1BackupName = "library.v1.bak";
    static constexpr auto kVersion2BackupName = "library.v2.bak";
    static constexpr auto kVersion3BackupName = "library.v3.bak";

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
    /** Folder holding a document's figure images. */
    [[nodiscard]] static QString assetsDirectory(qint64 documentId);

    /** Opens (creating and migrating if needed). Safe to call repeatedly. */
    bool open();
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QSqlDatabase connection() const;

    // Study sets
    [[nodiscard]] std::optional<qint64> addStudySet(const QString& name);
    bool renameStudySet(qint64 id, const QString& name);
    /** Deletes the study set and, by cascade, its documents, chunks, questions, attempts, and stats. */
    bool removeStudySet(qint64 id);
    [[nodiscard]] QVector<StudySet> studySets();

    // Documents
    [[nodiscard]] std::optional<qint64> addDocument(qint64 setId, const QString& title,
                                                    const QString& originalPath, DocumentKind kind);
    bool setDocumentStoredPath(qint64 id, const QString& storedPath);
    bool setDocumentStatus(qint64 id, DocumentStatus status, const QString& error = QString());
    bool setDocumentExtraction(qint64 id, int unitCount, const QList<int>& unsupportedUnits);
    bool setDocumentSelected(qint64 id, bool selected);
    [[nodiscard]] std::optional<DocumentRecord> document(qint64 id);
    /** Documents in a study set with chunk and question counts; all studySets when setId is 0. */
    [[nodiscard]] QVector<DocumentRecord> documents(qint64 setId);
    bool removeDocument(qint64 id);

    // Chunks
    /** Replaces all chunks of a document in one transaction (FTS index kept in sync by triggers). */
    bool replaceChunks(qint64 documentId, const QVector<Chunk>& chunks);
    [[nodiscard]] QVector<Chunk> chunks(qint64 documentId);
    [[nodiscard]] std::optional<Chunk> chunk(qint64 chunkId);

    // Questions
    /** Inserts or replaces a generated question (format-specific fields stored as JSON). */
    bool saveQuestion(const Question& question, qint64 chunkId, const QString& model);
    [[nodiscard]] std::optional<Question> question(const QString& id);
    [[nodiscard]] QVector<Question> documentQuestions(qint64 documentId);
    /**
     * Questions from a study set, optionally limited to some formats. When selectedOnly is
     * set, only documents the student selected for studying contribute.
     */
    [[nodiscard]] QVector<Question> setQuestions(qint64 setId, const QList<QuestionType>& types = {},
                                                    bool selectedOnly = true);
    bool removeQuestions(qint64 documentId);
    /** Updates a question in place (keeps its ID, attempts, and review schedule). */
    bool updateQuestion(const Question& question);
    bool removeQuestion(const QString& id);

    /** The set's "My questions" document, created on first use; holds questions the student wrote. */
    [[nodiscard]] std::optional<qint64> manualDocument(qint64 setId);

    // Figures, tables and data shown beside questions
    [[nodiscard]] std::optional<qint64> addSupportAsset(const SupportAsset& asset);
    [[nodiscard]] std::optional<SupportAsset> supportAsset(qint64 id);
    /** A document's assets; unitNumber below 1 returns all of them. */
    [[nodiscard]] QVector<SupportAsset> supportAssets(qint64 documentId, int unitNumber = 0);
    /** Deletes a document's asset rows and their image files. */
    bool removeSupportAssets(qint64 documentId);
    /** Stores what a vision model read in an image (computed once, reused for every question). */
    bool setAssetInterpretation(qint64 id, const QString& interpretation, const QString& model, bool legible,
                                const QString& readableText = QString());
    /** Fills each question's asset references with the stored asset (path, table, interpretation). */
    void resolveSupport(QVector<Question>& questions);
    void resolveSupport(Question& question);

    // Pages or slides whose text came from OCR
    bool setUnitOcr(qint64 documentId, int unitNumber, double confidence);
    bool clearOcrUnits(qint64 documentId);
    [[nodiscard]] QHash<int, double> ocrUnits(qint64 documentId);
    [[nodiscard]] QSet<qint64> usedChunkIds(qint64 documentId);

    /** Marks documents left mid-processing by a previous run as failed so they can be retried. */
    void recoverInterruptedDocuments();

private:
    bool exec(const QString& sql);
    bool migrate();

    QString m_connectionName;
    QString m_lastError;
};
