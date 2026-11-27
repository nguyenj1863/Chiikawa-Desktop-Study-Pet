#pragma once

#include "ai/LlmClient.h"

#include <QList>
#include <QNetworkReply>
#include <QPointer>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;

/**
 * Talks to a local Ollama server over loopback HTTP only (127.0.0.1 / localhost / ::1).
 * Non-loopback base URLs are rejected, so documents and answers never leave the
 * machine. Requests are serialized and each has its own timeout and cancellation.
 *
 * Also reports whether Ollama is installed, running, and has the chosen model,
 * can start an installed Ollama, and can pull a model, but only when the user asks.
 */
class OllamaClient : public LlmClient {
    Q_OBJECT

public:
    static constexpr auto kDefaultModel = "qwen3.5:4b-q4_K_M";
    static constexpr auto kDefaultBaseUrl = "http://127.0.0.1:11434";
    static constexpr auto kDownloadPage = "https://ollama.com/download";

    enum class SetupState {
        Unknown,
        Checking,
        NotInstalled,   // no Ollama executable found and no server answering
        NotRunning,     // installed but the server is not answering
        ModelMissing,   // server answering, chosen model not installed
        Ready
    };
    Q_ENUM(SetupState)

    explicit OllamaClient(QObject* parent = nullptr);
    ~OllamaClient() override;

    /** Returns false (and keeps the previous URL) unless the host is loopback. */
    bool setBaseUrl(const QUrl& url);
    [[nodiscard]] QUrl baseUrl() const { return m_baseUrl; }
    [[nodiscard]] static bool isLoopback(const QUrl& url);

    void setModel(const QString& model);
    [[nodiscard]] QString model() const { return m_model; }

    /** Executables checked to tell "not installed" from "not running" (overridable for tests). */
    void setExecutableCandidates(const QStringList& paths) { m_executables = paths; }
    [[nodiscard]] static QStringList defaultExecutableCandidates();
    [[nodiscard]] QString installedExecutable() const;

    // LlmClient
    int generate(const LlmRequest& request) override;
    void cancel(int requestId) override;
    void cancelAll() override;
    [[nodiscard]] bool isBusy() const override { return m_active != nullptr || !m_queue.isEmpty(); }
    void releaseModel() override;

    // Setup
    void checkSetup();
    [[nodiscard]] SetupState setupState() const { return m_state; }
    [[nodiscard]] QStringList installedModels() const { return m_models; }
    [[nodiscard]] static QString setupMessage(SetupState state, const QString& model);
    [[nodiscard]] static QString approximateDownloadSize(const QString& model);

    /** Launches an installed Ollama in the background and re-checks until it answers (or gives up). */
    bool startOllama();

    /** Asks Ollama to download a model. Call only after the user confirmed. */
    void pullModel(const QString& model);
    void cancelPull();
    [[nodiscard]] bool isPulling() const { return !m_pullReply.isNull(); }

    /** Classifies a pull error, e.g. an offline machine, into a user-facing message. */
    [[nodiscard]] static QString describePullError(const QString& raw);

    void setStartPollLimit(int attempts, int intervalMs);

signals:
    void setupStateChanged(OllamaClient::SetupState state);
    void pullProgress(qint64 completed, qint64 total, const QString& status);
    void pullFinished(bool ok, const QString& message);

private:
    struct Pending {
        int id = 0;
        LlmRequest request;
        bool retriedWithoutThink = false;
    };

    void startNext();
    void sendActive();
    void finishActive();
    void onChatReply();
    void onTagsReply(QNetworkReply* reply);
    void setState(SetupState state);
    [[nodiscard]] QUrl endpoint(const QString& path) const;
    [[nodiscard]] bool modelInstalled(const QString& model) const;

    QNetworkAccessManager* m_net = nullptr;
    QUrl m_baseUrl;
    QString m_model;
    QStringList m_executables;
    QStringList m_models;
    SetupState m_state = SetupState::Unknown;

    QQueue<Pending> m_queue;
    Pending m_activeRequest;
    QPointer<QNetworkReply> m_active;
    QTimer m_timeout;
    bool m_timedOut = false;
    int m_nextId = 1;

    QPointer<QNetworkReply> m_tagsReply;
    QPointer<QNetworkReply> m_pullReply;
    QByteArray m_pullBuffer;
    QString m_pullError;
    bool m_pullSucceeded = false;

    QTimer m_startPoll;
    int m_startPollRemaining = 0;
    int m_startPollLimit = 20;
};
