#include "ai/OllamaClient.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <functional>
#include <memory>

namespace {

/** Minimal HTTP/1.1 server imitating the Ollama endpoints the client uses. */
class FakeOllama : public QObject {
    Q_OBJECT

public:
    struct Response {
        int status = 200;
        QByteArray body;
        bool hang = false;              // never answer (timeouts, cancellation)
        QList<QByteArray> stream;       // NDJSON lines streamed with a short delay
    };
    using Handler = std::function<Response(const QString& path, const QJsonObject& body)>;

    FakeOllama()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }

    Handler handler;
    QList<QPair<QString, QJsonObject>> requests;
    QList<QByteArray> rawBodies;
    int inFlight = 0;
    int maxInFlight = 0;

    static QByteArray json(const QJsonObject& obj) { return QJsonDocument(obj).toJson(QJsonDocument::Compact); }
    static QByteArray chatBody(const QString& content)
    {
        return json({{QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                              {QStringLiteral("content"), content}}},
                     {QStringLiteral("done"), true}});
    }
    static QByteArray tagsBody(const QStringList& names)
    {
        QJsonArray models;
        for (const QString& n : names) {
            models.append(QJsonObject{{QStringLiteral("name"), n}});
        }
        return json({{QStringLiteral("models"), models}});
    }

private:
    void onReadyRead(QTcpSocket* socket)
    {
        QByteArray& buf = m_buffers[socket];
        buf += socket->readAll();
        const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        const QByteArray head = buf.left(headerEnd);
        qsizetype length = 0;
        for (const QByteArray& line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                length = line.mid(15).trimmed().toLongLong();
            }
        }
        if (buf.size() < headerEnd + 4 + length) {
            return;
        }
        const QByteArray body = buf.mid(headerEnd + 4, length);
        const QList<QByteArray> requestLine = head.left(head.indexOf('\r')).split(' ');
        const QString path = requestLine.size() > 1 ? QString::fromLatin1(requestLine.at(1)) : QString();
        m_buffers.remove(socket);

        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        requests.append({path, obj});
        rawBodies.append(body);
        ++inFlight;
        maxInFlight = std::max(maxInFlight, inFlight);
        const Response r = handler ? handler(path, obj) : Response{404, json({{QStringLiteral("error"), QStringLiteral("no handler")}})};
        if (r.hang) {
            connect(socket, &QTcpSocket::disconnected, this, [this]() { --inFlight; });
            return;
        }
        if (!r.stream.isEmpty()) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n");
            QPointer<QTcpSocket> guard(socket);
            auto lines = std::make_shared<QList<QByteArray>>(r.stream);
            auto* timer = new QTimer(socket);
            timer->setInterval(15);
            connect(timer, &QTimer::timeout, this, [this, guard, lines, timer]() {
                if (!guard) {
                    return;
                }
                if (lines->isEmpty()) {
                    timer->stop();
                    --inFlight;
                    guard->disconnectFromHost();
                    return;
                }
                guard->write(lines->takeFirst() + '\n');
                guard->flush();
            });
            timer->start();
            return;
        }
        const QByteArray reason = r.status == 200 ? "OK" : "Error";
        socket->write("HTTP/1.1 " + QByteArray::number(r.status) + ' ' + reason
                      + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(r.body.size())
                      + "\r\nConnection: close\r\n\r\n" + r.body);
        socket->flush();
        --inFlight;
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

quint16 closedPort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 port = s.serverPort();
    s.close();
    return port;
}

LlmRequest simpleRequest(int timeoutMs = 5000)
{
    LlmRequest r;
    r.system = QStringLiteral("You write quiz questions.");
    r.prompt = QStringLiteral("Make one question.");
    r.timeoutMs = timeoutMs;
    return r;
}

} // namespace

class TestOllamaClient : public QObject {
    Q_OBJECT

private slots:
    void acceptsOnlyLoopbackUrls();
    void setupReportsNotInstalledOrNotRunning();
    void setupDetectsModels();
    void chatSendsStructuredLocalRequest();
    void requestsRunOneAtATime();
    void reportsModelMissingAndHttpErrors();
    void retriesWithoutThinkWhenRejected();
    void timesOutHungRequests();
    void cancelsQueuedAndActiveRequests();
    void reportsNotRunningAndEmptyAnswers();
    void pullStreamsProgressAndRechecks();
    void pullReportsOfflineAndCancellation();
    void releaseModelUnloadsWithKeepAliveZero();
    void startOllamaNeedsAnInstall();
};

void TestOllamaClient::acceptsOnlyLoopbackUrls()
{
    OllamaClient client;
    QVERIFY(OllamaClient::isLoopback(QUrl(QStringLiteral("http://127.0.0.1:11434"))));
    QVERIFY(OllamaClient::isLoopback(QUrl(QStringLiteral("http://localhost:11434"))));
    QVERIFY(OllamaClient::isLoopback(QUrl(QStringLiteral("http://[::1]:11434"))));
    QVERIFY(!client.setBaseUrl(QUrl(QStringLiteral("http://example.com:11434"))));
    QVERIFY(!client.setBaseUrl(QUrl(QStringLiteral("http://192.168.1.20:11434"))));
    QVERIFY(!client.setBaseUrl(QUrl(QStringLiteral("https://localhost:11434"))));
    QVERIFY(!client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1.evil.com"))));
    QCOMPARE(client.baseUrl(), QUrl(QString::fromLatin1(OllamaClient::kDefaultBaseUrl)));
    QCOMPARE(client.model(), QString::fromLatin1(OllamaClient::kDefaultModel));
}

void TestOllamaClient::setupReportsNotInstalledOrNotRunning()
{
    OllamaClient client;
    QVERIFY(client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()))));
    client.setExecutableCandidates({QStringLiteral("C:/definitely/missing/ollama.exe")});
    QSignalSpy states(&client, &OllamaClient::setupStateChanged);
    client.checkSetup();
    QCOMPARE(client.setupState(), OllamaClient::SetupState::Checking);
    QTRY_COMPARE_WITH_TIMEOUT(client.setupState(), OllamaClient::SetupState::NotInstalled, 5000);
    QVERIFY(OllamaClient::setupMessage(OllamaClient::SetupState::NotInstalled, client.model()).contains(QStringLiteral("ollama.com")));

    QTemporaryFile fakeExe;
    QVERIFY(fakeExe.open());
    client.setExecutableCandidates({QStringLiteral("C:/missing.exe"), fakeExe.fileName()});
    QCOMPARE(client.installedExecutable(), QFileInfo(fakeExe.fileName()).absoluteFilePath());
    client.checkSetup();
    QTRY_COMPARE_WITH_TIMEOUT(client.setupState(), OllamaClient::SetupState::NotRunning, 5000);
    QVERIFY(states.count() >= 4);
}

void TestOllamaClient::setupDetectsModels()
{
    FakeOllama server;
    server.handler = [](const QString& path, const QJsonObject&) {
        if (path == QStringLiteral("/api/tags")) {
            return FakeOllama::Response{200, FakeOllama::tagsBody({QStringLiteral("llama3.2:latest"),
                                                                    QStringLiteral("mimo-v2.6:latest")})};
        }
        return FakeOllama::Response{404, {}};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    client.checkSetup();
    QTRY_COMPARE_WITH_TIMEOUT(client.setupState(), OllamaClient::SetupState::ModelMissing, 5000);
    QCOMPARE(client.installedModels().size(), 2);
    const QString message = OllamaClient::setupMessage(client.setupState(), client.model());
    QVERIFY(message.contains(QStringLiteral("ollama pull qwen3.5:4b-q4_K_M")));
    QVERIFY(message.contains(QStringLiteral("3.3 GB")));

    client.setModel(QStringLiteral("llama3.2"));   // implicit :latest tag
    QCOMPARE(client.setupState(), OllamaClient::SetupState::Ready);
    client.setModel(QStringLiteral("other:7b"));
    QCOMPARE(client.setupState(), OllamaClient::SetupState::ModelMissing);
    QCOMPARE(OllamaClient::approximateDownloadSize(QStringLiteral("other:7b")), QStringLiteral("several GB"));
}

void TestOllamaClient::chatSendsStructuredLocalRequest()
{
    FakeOllama server;
    server.handler = [](const QString&, const QJsonObject&) {
        return FakeOllama::Response{200, FakeOllama::chatBody(QStringLiteral("{\"answer\":42}"))};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy finished(&client, &LlmClient::finished);
    QSignalSpy failed(&client, &LlmClient::failed);

    LlmRequest req = simpleRequest();
    req.schema = R"({"type":"object","properties":{"zeta":{"type":"string"},"alpha":{"type":"string"}}})";
    req.temperature = 0.1;
    req.maxTokens = 321;
    const int id = client.generate(req);
    QVERIFY(client.isBusy());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(finished.first().at(0).toInt(), id);
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("{\"answer\":42}"));
    QVERIFY(!client.isBusy());

    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.first().first, QStringLiteral("/api/chat"));
    const QJsonObject body = server.requests.first().second;
    QCOMPARE(body.value(QStringLiteral("model")).toString(), QString::fromLatin1(OllamaClient::kDefaultModel));
    QCOMPARE(body.value(QStringLiteral("stream")).toBool(true), false);
    QCOMPARE(body.value(QStringLiteral("think")).toBool(true), false);
    QCOMPARE(body.value(QStringLiteral("format")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("object"));
    // Property order is preserved on the wire (zeta before alpha).
    QVERIFY(server.rawBodies.first().indexOf("zeta") < server.rawBodies.first().indexOf("alpha"));
    QVERIFY(!body.value(QStringLiteral("keep_alive")).toString().isEmpty());
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("Make one question."));
    const QJsonObject options = body.value(QStringLiteral("options")).toObject();
    QCOMPARE(options.value(QStringLiteral("num_predict")).toInt(), 321);
    QCOMPARE(options.value(QStringLiteral("temperature")).toDouble(), 0.1);
    QCOMPARE(client.setupState(), OllamaClient::SetupState::Ready);
}

void TestOllamaClient::requestsRunOneAtATime()
{
    FakeOllama server;
    int n = 0;
    server.handler = [&n](const QString&, const QJsonObject&) {
        return FakeOllama::Response{200, FakeOllama::chatBody(QStringLiteral("reply %1").arg(++n))};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy finished(&client, &LlmClient::finished);
    const int a = client.generate(simpleRequest());
    const int b = client.generate(simpleRequest());
    const int c = client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 5000);
    QCOMPARE(server.maxInFlight, 1);
    QCOMPARE(finished.at(0).at(0).toInt(), a);
    QCOMPARE(finished.at(1).at(0).toInt(), b);
    QCOMPARE(finished.at(2).at(0).toInt(), c);
    QCOMPARE(finished.at(2).at(1).toString(), QStringLiteral("reply 3"));
}

void TestOllamaClient::reportsModelMissingAndHttpErrors()
{
    FakeOllama server;
    int call = 0;
    server.handler = [&call](const QString&, const QJsonObject&) {
        if (++call == 1) {
            return FakeOllama::Response{404, FakeOllama::json({{QStringLiteral("error"),
                                                                QStringLiteral("model \"qwen3.5:4b-q4_K_M\" not found, try pulling it first")}})};
        }
        return FakeOllama::Response{500, FakeOllama::json({{QStringLiteral("error"), QStringLiteral("out of memory")}})};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy failed(&client, &LlmClient::failed);
    client.generate(simpleRequest());
    client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
    QCOMPARE(failed.at(0).at(1).value<LlmError>().kind, LlmErrorKind::ModelMissing);
    QCOMPARE(client.setupState(), OllamaClient::SetupState::ModelMissing);
    const LlmError http = failed.at(1).at(1).value<LlmError>();
    QCOMPARE(http.kind, LlmErrorKind::Http);
    QCOMPARE(http.message, QStringLiteral("out of memory"));
}

void TestOllamaClient::retriesWithoutThinkWhenRejected()
{
    FakeOllama server;
    server.handler = [](const QString&, const QJsonObject& body) {
        if (body.contains(QStringLiteral("think"))) {
            return FakeOllama::Response{400, FakeOllama::json({{QStringLiteral("error"),
                                                                QStringLiteral("\"x\" does not support thinking")}})};
        }
        return FakeOllama::Response{200, FakeOllama::chatBody(QStringLiteral("ok"))};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy finished(&client, &LlmClient::finished);
    client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(server.requests.size(), 2);
    QVERIFY(!server.requests.at(1).second.contains(QStringLiteral("think")));
}

void TestOllamaClient::timesOutHungRequests()
{
    FakeOllama server;
    int call = 0;
    server.handler = [&call](const QString&, const QJsonObject&) {
        if (++call == 1) {
            return FakeOllama::Response{200, {}, true};
        }
        return FakeOllama::Response{200, FakeOllama::chatBody(QStringLiteral("after timeout"))};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy failed(&client, &LlmClient::failed);
    QSignalSpy finished(&client, &LlmClient::finished);
    client.generate(simpleRequest(1000));
    client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.first().at(1).value<LlmError>().kind, LlmErrorKind::Timeout);
    // The queue keeps going after a timeout.
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("after timeout"));
}

void TestOllamaClient::cancelsQueuedAndActiveRequests()
{
    FakeOllama server;
    server.handler = [](const QString&, const QJsonObject&) { return FakeOllama::Response{200, {}, true}; };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy failed(&client, &LlmClient::failed);
    const int active = client.generate(simpleRequest());
    const int queued = client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);

    client.cancel(queued);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toInt(), queued);
    QCOMPARE(failed.at(0).at(1).value<LlmError>().kind, LlmErrorKind::Cancelled);

    client.cancel(active);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
    QCOMPARE(failed.at(1).at(0).toInt(), active);
    QCOMPARE(failed.at(1).at(1).value<LlmError>().kind, LlmErrorKind::Cancelled);
    QVERIFY(!client.isBusy());

    client.generate(simpleRequest());
    client.generate(simpleRequest());
    client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
    client.cancelAll();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 5, 5000);
    QVERIFY(!client.isBusy());
    QTest::qWait(100);
    QCOMPARE(server.requests.size(), 2);   // cancelled queue entries were never sent
}

void TestOllamaClient::reportsNotRunningAndEmptyAnswers()
{
    {
        OllamaClient client;
        QVERIFY(client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()))));
        client.setExecutableCandidates({});
        QSignalSpy failed(&client, &LlmClient::failed);
        client.generate(simpleRequest());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        const LlmError err = failed.first().at(1).value<LlmError>();
        QCOMPARE(err.kind, LlmErrorKind::NotRunning);
        QVERIFY(err.message.contains(QStringLiteral("Ollama")));
        QCOMPARE(client.setupState(), OllamaClient::SetupState::NotInstalled);
    }
    FakeOllama server;
    server.handler = [](const QString&, const QJsonObject&) {
        return FakeOllama::Response{200, FakeOllama::chatBody(QStringLiteral("   "))};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy failed(&client, &LlmClient::failed);
    client.generate(simpleRequest());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.first().at(1).value<LlmError>().kind, LlmErrorKind::BadResponse);
}

void TestOllamaClient::pullStreamsProgressAndRechecks()
{
    FakeOllama server;
    bool pulled = false;
    server.handler = [&pulled](const QString& path, const QJsonObject& body) {
        if (path == QStringLiteral("/api/pull")) {
            pulled = body.value(QStringLiteral("model")).toString() == QStringLiteral("qwen3.5:4b-q4_K_M");
            FakeOllama::Response r;
            r.stream = {
                R"({"status":"pulling manifest"})",
                R"({"status":"pulling abc","total":1000,"completed":250})",
                R"({"status":"pulling abc","total":1000,"completed":1000})",
                R"({"status":"verifying sha256 digest"})",
                R"({"status":"success"})",
            };
            return r;
        }
        return FakeOllama::Response{200, FakeOllama::tagsBody(pulled ? QStringList{QStringLiteral("qwen3.5:4b-q4_K_M")}
                                                                     : QStringList{})};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy progress(&client, &OllamaClient::pullProgress);
    QSignalSpy done(&client, &OllamaClient::pullFinished);
    client.pullModel(client.model());
    QVERIFY(client.isPulling());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
    QCOMPARE(progress.count(), 5);
    QCOMPARE(progress.at(1).at(0).toLongLong(), 250);
    QCOMPARE(progress.at(1).at(1).toLongLong(), 1000);
    QVERIFY(!client.isPulling());
    QTRY_COMPARE_WITH_TIMEOUT(client.setupState(), OllamaClient::SetupState::Ready, 5000);
}

void TestOllamaClient::pullReportsOfflineAndCancellation()
{
    FakeOllama server;
    bool hang = false;
    server.handler = [&hang](const QString&, const QJsonObject&) {
        if (hang) {
            return FakeOllama::Response{200, {}, true};
        }
        FakeOllama::Response r;
        r.stream = {
            R"({"status":"pulling manifest"})",
            R"({"error":"pull model manifest: Get \"https://registry.ollama.ai/v2/library/qwen3.5/manifests/4b\": dial tcp: lookup registry.ollama.ai: no such host"})",
        };
        return r;
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    QSignalSpy done(&client, &OllamaClient::pullFinished);
    client.pullModel(client.model());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(done.first().at(0).toBool(), false);
    QVERIFY2(done.first().at(1).toString().contains(QStringLiteral("offline")), qPrintable(done.first().at(1).toString()));

    hang = true;
    client.pullModel(client.model());
    QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
    client.cancelPull();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 5000);
    QCOMPARE(done.at(1).at(0).toBool(), false);
    QCOMPARE(done.at(1).at(1).toString(), QStringLiteral("Download cancelled."));

    QVERIFY(OllamaClient::describePullError(QStringLiteral("write /x: no space left on device")).contains(QStringLiteral("disk space")));
    QVERIFY(OllamaClient::describePullError(QStringLiteral("pull model manifest: file does not exist")).contains(QStringLiteral("not found")));
}

void TestOllamaClient::releaseModelUnloadsWithKeepAliveZero()
{
    FakeOllama server;
    server.handler = [](const QString&, const QJsonObject&) {
        return FakeOllama::Response{200, FakeOllama::json({{QStringLiteral("done"), true}})};
    };
    OllamaClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    client.releaseModel();
    QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
    QCOMPARE(server.requests.first().first, QStringLiteral("/api/generate"));
    QCOMPARE(server.requests.first().second.value(QStringLiteral("keep_alive")).toInt(-1), 0);
}

void TestOllamaClient::startOllamaNeedsAnInstall()
{
    OllamaClient client;
    client.setExecutableCandidates({QStringLiteral("C:/nope/ollama.exe")});
    QVERIFY(!client.startOllama());
}

QTEST_MAIN(TestOllamaClient)
#include "test_ollama_client.moc"
