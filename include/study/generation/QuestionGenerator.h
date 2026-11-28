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
     * Vision-capable model used to read figures before writing questions about them. Empty turns
     * figure reading off: figures are still shown, but questions stay about the text.
     */
    void setVisionModel(const QString& model) { m_visionModel = model; }
    [[nodiscard]] QString visionModel() const { return m_visionModel; }
    /** Reads every figure of a document that has not been read yet ("Read figures"). */
    bool readFigures(qint64 documentId);

    static constexpr int kMaxFiguresPerRun = 8;        // figures read before a Generate run
    static constexpr int kMaxFiguresPerDocument = 40;  // figures read by "Read figures"

    /**
     * Starts generating for the documents. With regenerate, existing questions of
     * those documents are deleted first (their attempts go with them); otherwise new
     * questions come from chunks not used yet. Returns false if already running or
     * there is nothing to do.
     */
    bool start(const QList<qint64>& documentIds, const QList<QuestionType>& formats, bool regenerate);
    /**
     * Writes one new question from the same passage and in the same format as an existing one
     * (worded differently). The old question is left alone; questionRegenerated() reports both IDs
     * so the student can choose to replace it or keep both.
     */
    bool regenerateQuestion(const QString& questionId);
    void cancel();
    [[nodiscard]] bool isRunning() const { return m_running; }

    // Pure helpers (exposed for tests)
    /**
     * Schema as ordered JSON text: concept and evidence come before the question and answers.
     * With visuals, a "visual" number (0 = none) says which shown table or figure the question uses.
     */
    [[nodiscard]] static QByteArray schemaFor(QuestionType type, int visualCount = 0);
    /**
     * Prompt for one passage. visuals are the tables and interpreted figures on the same slide or
     * page; the student sees the one a question uses. textOnly forbids referring to any visual.
     * contextRetry adds a note that the previous try depended on something the student cannot see.
     */
    [[nodiscard]] static LlmRequest buildRequest(QuestionType type, const Chunk& chunk, const QString& documentTitle,
                                                 const QStringList& avoidConcepts,
                                                 const QVector<SupportAsset>& visuals = {}, bool textOnly = false,
                                                 bool contextRetry = false);
    /**
     * Parses a model reply into a question (without IDs or source fields). Sets
     * *unsupported when a multi-select reply says the passage cannot support one, and *visual
     * to the number of the visual the question uses (0 for none).
     */
    [[nodiscard]] static std::optional<Question> parseResponse(QuestionType type, const QString& json,
                                                               QString* error, bool* unsupported = nullptr,
                                                               int* visual = nullptr);
    /** A table or interpreted figure written out as text, for the prompt and the evidence check. */
    [[nodiscard]] static QString describeVisual(const SupportAsset& asset);
    /** Tables and figures a vision model has read on the chunk's slide or page. */
    [[nodiscard]] static QVector<SupportAsset> usableVisuals(const QVector<SupportAsset>& assets, const Chunk& chunk);
    [[nodiscard]] static QString stableId(qint64 documentId, QuestionType type, const QString& prompt);
    /** Evenly spaced chunks with enough text, skipping already used ones; at most maxCount. */
    [[nodiscard]] static QVector<Chunk> pickChunks(const QVector<Chunk>& chunks, const QSet<qint64>& used, int maxCount);

signals:
    void progress(int done, int total, const QString& status);
    void documentStarted(qint64 documentId);
    void documentFinished(qint64 documentId, int accepted, int rejected);
    /** message is empty on normal completion; otherwise explains why generation stopped. */
    void finished(int accepted, int rejected, const QString& message);
    /** A new version of oldId was saved as newId. */
    void questionRegenerated(const QString& oldId, const QString& newId);

private:
    struct Job {
        qint64 documentId = 0;
        qint64 setId = 0;
        QString title;
        Chunk chunk;
        QuestionType type = QuestionType::SingleChoice;
        int attempts = 0;
        bool fellBack = false;
        QVector<SupportAsset> visuals;   // what the student could be shown with this passage
        bool textOnly = false;           // a previous try referred to a visual nobody would see
        bool contextRetry = false;       // a previous try depended on something the student cannot see
        QString replacing;               // regenerating this question
    };
    struct FigureJob {
        SupportAsset asset;
        QString title;
        QString context;   // caption and nearby text, so the model names parts like the course does
    };
    struct DocState {
        int pending = 0;
        int accepted = 0;
        int rejected = 0;
        QStringList concepts;
        QStringList prompts;
        QSet<int> scannedUnits;   // pages or slides whose text came from OCR
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
    void queueFigures(qint64 documentId, const QVector<Chunk>& chunks, const QString& title, int limit);
    void submitFigure();
    void onFigureFinished(const QString& content);
    [[nodiscard]] static QByteArray baseSchemaFor(QuestionType type, const QByteArray& difficulty);

    StudyDatabase* m_db = nullptr;
    LlmClient* m_llm = nullptr;
    QString m_modelName;
    QString m_visionModel;
    QQueue<FigureJob> m_figures;
    FigureJob m_currentFigure;
    bool m_readingFigure = false;
    bool m_figuresOnly = false;
    int m_figuresRead = 0;
    int m_figuresUnreadable = 0;
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
