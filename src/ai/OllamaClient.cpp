#include "ai/OllamaClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>

namespace {

constexpr int kTagsTimeoutMs = 3000;
constexpr int kNumCtx = 8192;
// Keep the model warm only briefly between queued requests; releaseModel() unloads it after a batch.
const QString kKeepAlive = QStringLiteral("2m");

QString replyErrorText(QNetworkReply* reply, const QByteArray& body)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString serverError = obj.value(QStringLiteral("error")).toString();
    return serverError.isEmpty() ? reply->errorString() : serverError;
}

bool isConnectionFailure(QNetworkReply::NetworkError error)
{
    return error == QNetworkReply::ConnectionRefusedError || error == QNetworkReply::HostNotFoundError
        || error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::UnknownNetworkError
        || error == QNetworkReply::NetworkSessionFailedError;
}

} // namespace

OllamaClient::OllamaClient(QObject* parent)
    : LlmClient(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(QString::fromLatin1(kDefaultBaseUrl))
    , m_model(QString::fromLatin1(kDefaultModel))
    , m_executables(defaultExecutableCandidates())
{
    qRegisterMetaType<LlmError>();
    // Loopback traffic must never be routed through a system proxy.
    m_net->setProxy(QNetworkProxy::NoProxy);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_active) {
            m_timedOut = true;
            m_active->abort();
        }
    });
    m_startPoll.setInterval(1000);
    // After startOllama(), re-check once per interval until the server answers or attempts run out.
    connect(&m_startPoll, &QTimer::timeout, this, [this]() {
        if (m_state == SetupState::Ready || m_state == SetupState::ModelMissing || m_startPollRemaining <= 0) {
            m_startPoll.stop();
            return;
        }
        --m_startPollRemaining;
        checkSetup();
    });
}

OllamaClient::~OllamaClient()
{
    m_startPoll.stop();
    m_queue.clear();
    if (m_active) {
        m_active->disconnect(this);
        m_active->abort();
    }
    if (m_pullReply) {
        m_pullReply->disconnect(this);
        m_pullReply->abort();
    }
    if (m_tagsReply) {
        m_tagsReply->disconnect(this);
        m_tagsReply->abort();
    }
}

bool OllamaClient::isLoopback(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("http")) {
        return false;
    }
    const QString host = url.host();
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

bool OllamaClient::setBaseUrl(const QUrl& url)
{
    if (!isLoopback(url)) {
        return false;
    }
    m_baseUrl = url;
    return true;
}

void OllamaClient::setModel(const QString& model)
{
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || trimmed == m_model) {
        return;
    }
    m_model = trimmed;
    if (m_state == SetupState::Ready || m_state == SetupState::ModelMissing) {
        setState(modelInstalled(m_model) ? SetupState::Ready : SetupState::ModelMissing);
    }
}

QStringList OllamaClient::defaultExecutableCandidates()
{
    QStringList paths;
#if defined(Q_OS_WIN)
    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);   // %LOCALAPPDATA%
    paths << local + QStringLiteral("/Programs/Ollama/ollama.exe");
    paths << QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")) + QStringLiteral("/Ollama/ollama.exe");
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/Applications/Ollama.app/Contents/Resources/ollama")
          << QDir::homePath() + QStringLiteral("/Applications/Ollama.app/Contents/Resources/ollama")
          << QStringLiteral("/opt/homebrew/bin/ollama") << QStringLiteral("/usr/local/bin/ollama");
#else
    paths << QStringLiteral("/usr/local/bin/ollama") << QStringLiteral("/usr/bin/ollama");
#endif
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ollama"));
    if (!onPath.isEmpty()) {
        paths << onPath;
    }
    return paths;
}

QString OllamaClient::installedExecutable() const
{
    for (const QString& path : m_executables) {
        const QFileInfo info(path);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

QUrl OllamaClient::endpoint(const QString& path) const
{
    QUrl url = m_baseUrl;
    url.setPath(path);
    return url;
}

bool OllamaClient::modelInstalled(const QString& model) const
{
    const QString wanted = model.contains(QLatin1Char(':')) ? model : model + QStringLiteral(":latest");
    for (const QString& m : m_models) {
        if (m.compare(wanted, Qt::CaseInsensitive) == 0 || m.compare(model, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

int OllamaClient::generate(const LlmRequest& request)
{
    Pending p;
    p.id = m_nextId++;
    p.request = request;
    m_queue.enqueue(p);
    QMetaObject::invokeMethod(this, &OllamaClient::startNext, Qt::QueuedConnection);
    return p.id;
}

void OllamaClient::cancel(int requestId)
{
    for (qsizetype i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).id == requestId) {
            m_queue.removeAt(i);
            emit failed(requestId, {LlmErrorKind::Cancelled, QStringLiteral("Cancelled.")});
            return;
        }
    }
    if (m_active && m_activeRequest.id == requestId) {
        m_timedOut = false;
        m_active->abort();
    }
}

void OllamaClient::cancelAll()
{
    const QQueue<Pending> queued = m_queue;
    m_queue.clear();
    for (const Pending& p : queued) {
        emit failed(p.id, {LlmErrorKind::Cancelled, QStringLiteral("Cancelled.")});
    }
    if (m_active) {
        m_timedOut = false;
        m_active->abort();
    }
}

void OllamaClient::startNext()
{
    if (m_active || m_queue.isEmpty()) {
        return;
    }
    m_activeRequest = m_queue.dequeue();
    sendActive();
}

void OllamaClient::sendActive()
{
    const LlmRequest& r = m_activeRequest.request;
    QJsonArray messages;
    if (!r.system.isEmpty()) {
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), r.system}});
    }
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), r.prompt}});
    QJsonObject body{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), false},
        {QStringLiteral("keep_alive"), kKeepAlive},
        {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), r.temperature},
                                                {QStringLiteral("num_ctx"), kNumCtx},
                                                {QStringLiteral("num_predict"), r.maxTokens}}},
    };

    if (!m_activeRequest.retriedWithoutThink) {
        // Reasoning traces are slow on small local models and not needed for structured output.
        body.insert(QStringLiteral("think"), false);
    }

    QNetworkRequest req(endpoint(QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_timedOut = false;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (!r.schema.isEmpty() && QJsonDocument::fromJson(r.schema).isObject()) {
        // Splice the schema in as raw text: QJsonObject would sort its keys alphabetically.
        payload.chop(1);
        payload += R"(,"format":)" + r.schema.trimmed() + '}';
    }
    m_active = m_net->post(req, payload);
    connect(m_active, &QNetworkReply::finished, this, &OllamaClient::onChatReply);
    m_timeout.start(std::max(1000, r.timeoutMs));
}

void OllamaClient::onChatReply()
{
    QNetworkReply* reply = m_active;
    if (!reply) {
        return;
    }
    m_timeout.stop();
    const int id = m_activeRequest.id;
    // An aborted reply is closed; reading it would only log a warning.
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    const QNetworkReply::NetworkError error = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errorText = replyErrorText(reply, body);
    reply->deleteLater();
    m_active = nullptr;

    if (error == QNetworkReply::OperationCanceledError) {
        if (m_timedOut) {
            emit failed(id, {LlmErrorKind::Timeout, QStringLiteral("The local model took too long to answer.")});
        } else {
            emit failed(id, {LlmErrorKind::Cancelled, QStringLiteral("Cancelled.")});
        }
        finishActive();
        return;
    }
    if (status == 0 && isConnectionFailure(error)) {
        setState(installedExecutable().isEmpty() ? SetupState::NotInstalled : SetupState::NotRunning);
        emit failed(id, {LlmErrorKind::NotRunning, setupMessage(m_state, m_model)});
        finishActive();
        return;
    }
    if (status >= 400 || error != QNetworkReply::NoError) {
        // Older Ollama builds or non-reasoning models may reject the "think" field: retry once without it.
        if (status == 400 && errorText.contains(QStringLiteral("think"), Qt::CaseInsensitive)
            && !m_activeRequest.retriedWithoutThink) {
            m_activeRequest.retriedWithoutThink = true;
            sendActive();
            return;
        }
        if (status == 404 && errorText.contains(QStringLiteral("not found"), Qt::CaseInsensitive)) {
            setState(SetupState::ModelMissing);
            emit failed(id, {LlmErrorKind::ModelMissing, setupMessage(SetupState::ModelMissing, m_model)});
        } else {
            emit failed(id, {LlmErrorKind::Http, errorText});
        }
        finishActive();
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString content = obj.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    if (content.trimmed().isEmpty()) {
        emit failed(id, {LlmErrorKind::BadResponse, QStringLiteral("The local model returned an empty answer.")});
    } else {
        if (m_state != SetupState::Ready) {
            setState(SetupState::Ready);
        }
        emit finished(id, content);
    }
    finishActive();
}

void OllamaClient::finishActive()
{
    m_activeRequest = Pending{};
    startNext();
}

void OllamaClient::releaseModel()
{
    if (isBusy()) {
        return;
    }
    // keep_alive 0 asks Ollama to unload the model now instead of holding RAM/VRAM.
    QNetworkRequest req(endpoint(QStringLiteral("/api/generate")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("model"), m_model}, {QStringLiteral("keep_alive"), 0}};
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void OllamaClient::setState(SetupState state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    emit setupStateChanged(state);
}

void OllamaClient::checkSetup()
{
    if (m_tagsReply) {
        return;
    }
    setState(SetupState::Checking);
    QNetworkRequest req(endpoint(QStringLiteral("/api/tags")));
    req.setTransferTimeout(kTagsTimeoutMs);
    m_tagsReply = m_net->get(req);
    QNetworkReply* reply = m_tagsReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTagsReply(reply); });
}

void OllamaClient::onTagsReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (m_tagsReply == reply) {
        m_tagsReply = nullptr;
    }
    if (reply->error() != QNetworkReply::NoError) {
        m_models.clear();
        setState(installedExecutable().isEmpty() ? SetupState::NotInstalled : SetupState::NotRunning);
        return;
    }
    m_models.clear();
    const QJsonArray models = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue& v : models) {
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            m_models.append(name);
        }
    }
    setState(modelInstalled(m_model) ? SetupState::Ready : SetupState::ModelMissing);
}

QString OllamaClient::setupMessage(SetupState state, const QString& model)
{
    switch (state) {
    case SetupState::Unknown:
    case SetupState::Checking:
        return QStringLiteral("Checking for a local AI model…");
    case SetupState::NotInstalled:
        return QStringLiteral("Study questions are written by Ollama on this computer. "
                              "Install Ollama (free) from ollama.com, open it, then press Recheck.");
    case SetupState::NotRunning:
        return QStringLiteral("Ollama is installed but not running. Start it, then press Recheck.");
    case SetupState::ModelMissing:
        return QStringLiteral("Ollama is running, but the model %1 is not installed yet (%2 download, "
                              "internet needed once). You can download it here or run: ollama pull %1")
            .arg(model, approximateDownloadSize(model));
    case SetupState::Ready:
        return QStringLiteral("Ready: %1 runs locally on this computer.").arg(model);
    }
    return QString();
}

QString OllamaClient::approximateDownloadSize(const QString& model)
{
    if (model.compare(QString::fromLatin1(kDefaultModel), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("about 3.3 GB");
    }
    return QStringLiteral("several GB");
}

bool OllamaClient::startOllama()
{
    const QString exe = installedExecutable();
    if (exe.isEmpty()) {
        return false;
    }
    bool started = false;
#if defined(Q_OS_MACOS)
    if (exe.contains(QStringLiteral(".app/"))) {
        started = QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Ollama")});
    }
#elif defined(Q_OS_WIN)
    // The desktop app starts the server and its tray icon, like launching it from the Start menu.
    const QString app = QFileInfo(exe).dir().filePath(QStringLiteral("ollama app.exe"));
    if (QFileInfo::exists(app)) {
        started = QProcess::startDetached(app, {});
    }
#endif
    if (!started) {
        started = QProcess::startDetached(exe, {QStringLiteral("serve")});
    }
    if (started) {
        m_startPollRemaining = m_startPollLimit;
        m_startPoll.start();
    }
    return started;
}

void OllamaClient::setStartPollLimit(int attempts, int intervalMs)
{
    m_startPollLimit = std::max(1, attempts);
    m_startPoll.setInterval(std::max(10, intervalMs));
}

void OllamaClient::pullModel(const QString& model)
{
    if (m_pullReply || model.trimmed().isEmpty()) {
        return;
    }
    QNetworkRequest req(endpoint(QStringLiteral("/api/pull")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("model"), model.trimmed()}, {QStringLiteral("stream"), true}};
    m_pullBuffer.clear();
    m_pullError.clear();
    m_pullSucceeded = false;
    m_pullReply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply* reply = m_pullReply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        m_pullBuffer += reply->readAll();
        qsizetype nl = 0;
        while ((nl = m_pullBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_pullBuffer.left(nl).trimmed();
            m_pullBuffer.remove(0, nl + 1);
            if (line.isEmpty()) {
                continue;
            }
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            if (obj.contains(QStringLiteral("error"))) {
                m_pullError = obj.value(QStringLiteral("error")).toString();
                continue;
            }
            const QString status = obj.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("success")) {
                m_pullSucceeded = true;
            }
            emit pullProgress(obj.value(QStringLiteral("completed")).toInteger(),
                              obj.value(QStringLiteral("total")).toInteger(), status);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_pullReply = nullptr;
        const QNetworkReply::NetworkError error = reply->error();
        if (error == QNetworkReply::OperationCanceledError) {
            emit pullFinished(false, QStringLiteral("Download cancelled."));
            return;
        }
        if (m_pullError.isEmpty() && error != QNetworkReply::NoError) {
            m_pullError = replyErrorText(reply, m_pullBuffer);
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0 && isConnectionFailure(error)) {
                emit pullFinished(false, QStringLiteral("Ollama stopped responding. Make sure it is running, then try again."));
                return;
            }
        }
        if (!m_pullSucceeded || !m_pullError.isEmpty()) {
            emit pullFinished(false, describePullError(m_pullError));
            return;
        }
        emit pullFinished(true, QStringLiteral("Model downloaded."));
        checkSetup();
    });
}

void OllamaClient::cancelPull()
{
    if (m_pullReply) {
        m_pullReply->abort();
    }
}

QString OllamaClient::describePullError(const QString& raw)
{
    static const QStringList offlineHints = {
        QStringLiteral("dial tcp"), QStringLiteral("no such host"), QStringLiteral("lookup"),
        QStringLiteral("network is unreachable"), QStringLiteral("i/o timeout"),
        QStringLiteral("connection refused"), QStringLiteral("tls handshake"),
    };
    for (const QString& hint : offlineHints) {
        if (raw.contains(hint, Qt::CaseInsensitive)) {
            return QStringLiteral("You seem to be offline. Internet is needed once to download the model; "
                                  "after that everything works offline. Connect and try again.");
        }
    }
    if (raw.contains(QStringLiteral("file does not exist"), Qt::CaseInsensitive)
        || raw.contains(QStringLiteral("manifest unknown"), Qt::CaseInsensitive)) {
        return QStringLiteral("That model name was not found in the Ollama library.");
    }
    if (raw.contains(QStringLiteral("no space"), Qt::CaseInsensitive)) {
        return QStringLiteral("Not enough disk space to download the model.");
    }
    return raw.isEmpty() ? QStringLiteral("The download did not finish.") : QStringLiteral("Download failed: %1").arg(raw);
}
