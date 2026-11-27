#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

/**
 * Provider-neutral local LLM interface. OllamaClient is the first implementation;
 * an embedded engine (e.g. llama.cpp) or a test double can replace it without
 * touching question generation or answer evaluation.
 */

enum class LlmErrorKind {
    None,
    NotRunning,     // no local server reachable
    ModelMissing,   // server reachable, model not installed
    Timeout,
    Cancelled,
    Http,           // server returned an error status
    BadResponse     // reply was not the expected JSON
};

struct LlmError {
    LlmErrorKind kind = LlmErrorKind::None;
    QString message;
};

struct LlmRequest {
    QString system;
    QString prompt;
    /**
     * JSON schema the reply must follow, as raw JSON text so property order is kept
     * (grammar-constrained decoding writes fields in schema order). Empty for free text.
     */
    QByteArray schema;
    double temperature = 0.2;
    int maxTokens = 1024;
    int timeoutMs = 120000;
};

class LlmClient : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~LlmClient() override = default;

    /** Queues a request and returns its ID. Requests run one at a time, in order. */
    virtual int generate(const LlmRequest& request) = 0;
    virtual void cancel(int requestId) = 0;
    virtual void cancelAll() = 0;
    [[nodiscard]] virtual bool isBusy() const = 0;

    /** Lets the backend free model memory when a batch of work is done. */
    virtual void releaseModel() {}

signals:
    void finished(int requestId, const QString& content);
    void failed(int requestId, const LlmError& error);
};

Q_DECLARE_METATYPE(LlmError)
