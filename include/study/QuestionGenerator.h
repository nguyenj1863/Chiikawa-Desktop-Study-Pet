#pragma once

#include "ai/LlmClient.h"
#include "study/StudyTypes.h"
#include "study/Question.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <optional>

class StudyDatabase;

/**
 * Generates source-grounded questions for imported documents ahead of time, so
 * Chiikawa never waits on the model when asking. Chunks are spread across each
 * document, one concept per chunk; enabled formats rotate across chunks. Every
 * reply must match a JSON schema and pass QuestionValidator before it is saved.
 * Runs entirely on the UI thread's event loop: the LLM call is asynchronous
 * network I/O, and each step only does small database writes.
 */
class QuestionGenerator : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxQuestionsPerRun = 12;   // per document
    static constexpr int kMaxAttemptsPerChunk = 2;
    static constexpr int kMinChunkChars = 120;

    QuestionGenerator(StudyDatabase* db, LlmClient* llm, QObject* parent = nullptr);
    ~QuestionGenerator() override;

    void setModelName(const QString& model) { m_modelName = model; }

    /**
     * Starts generating for the documents. With regenerate, existing questions of
     * those documents are deleted first (their attempts go with them); otherwise new
     * questions come from chunks not used yet. Returns false if already running or
     * there is nothing to do.
     */
    bool start(const QList<qint64>& documentIds, const QList<QuestionType>& formats, bool regenerate);
    void cancel();
    [[nodiscard]] bool isRunning() const { return m_running; }

    // Pure helpers (exposed for tests)
    /** Schema as ordered JSON text: concept and evidence come before the question and answers. */
    [[nodiscard]] static QByteArray schemaFor(QuestionType type);
    [[nodiscard]] static LlmRequest buildRequest(QuestionType type, const Chunk& chunk, const QString& documentTitle,
                                                 const QStringList& avoidConcepts);
    /**
     * Parses a model reply into a question (without IDs or source fields). Sets
     * *unsupported when a multi-select reply says the passage cannot support one.
     */
    [[nodiscard]] static std::optional<Question> parseResponse(QuestionType type, const QString& json,
                                                               QString* error, bool* unsupported = nullptr);
    [[nodiscard]] static QString stableId(qint64 documentId, QuestionType type, const QString& prompt);
    /** Evenly spaced chunks with enough text, skipping already used ones; at most maxCount. */
    [[nodiscard]] static QVector<Chunk> pickChunks(const QVector<Chunk>& chunks, const QSet<qint64>& used, int maxCount);

signals:
    void progress(int done, int total, const QString& status);
    void documentStarted(qint64 documentId);
    void documentFinished(qint64 documentId, int accepted, int rejected);
    /** message is empty on normal completion; otherwise explains why generation stopped. */
    void finished(int accepted, int rejected, const QString& message);

private:
    struct Job {
        qint64 documentId = 0;
        qint64 setId = 0;
        QString title;
        Chunk chunk;
        QuestionType type = QuestionType::SingleChoice;
        int attempts = 0;
        bool fellBack = false;
    };
    struct DocState {
        int pending = 0;
        int accepted = 0;
        int rejected = 0;
        QStringList concepts;
        QStringList prompts;
    };

    void submitNext();
    void onFinished(int requestId, const QString& content);
    void onFailed(int requestId, const LlmError& error);
    void completeJob(const Job& job, bool accepted);
    void retryOrReject(Job job, const QString& reason);
    void abort(const QString& message);
    void finishDocument(qint64 documentId);
    void finishRun(const QString& message);
    [[nodiscard]] QuestionType fallbackFormat(QuestionType current) const;

    StudyDatabase* m_db = nullptr;
    LlmClient* m_llm = nullptr;
    QString m_modelName;
    QList<QuestionType> m_formats;
    QQueue<Job> m_jobs;
    Job m_current;
    int m_currentRequest = 0;
    QHash<qint64, DocState> m_docs;
    int m_total = 0;
    int m_done = 0;
    int m_accepted = 0;
    int m_rejected = 0;
    bool m_running = false;
};
