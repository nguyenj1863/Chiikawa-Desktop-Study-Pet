#pragma once

// Scripted LlmClient for tests: replies asynchronously (next event-loop turn) with
// content or an error chosen by a responder function. No network, no model.

#include "ai/LlmClient.h"

#include <QList>
#include <QTimer>
#include <functional>
#include <variant>

class MockLlmClient : public LlmClient {
    Q_OBJECT

public:
    using Reply = std::variant<QString, LlmError>;
    using Responder = std::function<Reply(const LlmRequest&)>;

    explicit MockLlmClient(QObject* parent = nullptr) : LlmClient(parent) {}

    Responder responder;
    QList<LlmRequest> requests;
    int releaseCount = 0;
    bool holdReplies = false;   // when set, requests stay pending until release() or cancel

    int generate(const LlmRequest& request) override
    {
        const int id = m_nextId++;
        requests.append(request);
        m_pending.append(id);
        if (!holdReplies) {
            QTimer::singleShot(0, this, [this, id, request]() { reply(id, request); });
        } else {
            m_held.append({id, request});
        }
        return id;
    }

    void releaseHeld()
    {
        const auto held = m_held;
        m_held.clear();
        for (const auto& h : held) {
            reply(h.first, h.second);
        }
    }

    void cancel(int requestId) override
    {
        if (m_pending.removeAll(requestId) > 0) {
            emit failed(requestId, {LlmErrorKind::Cancelled, QStringLiteral("Cancelled.")});
        }
    }

    void cancelAll() override
    {
        const QList<int> ids = m_pending;
        for (const int id : ids) {
            cancel(id);
        }
    }

    [[nodiscard]] bool isBusy() const override { return !m_pending.isEmpty(); }
    void releaseModel() override { ++releaseCount; }

    /** Which format a generation request asks for, judged from its schema. */
    static QString kindOf(const LlmRequest& r)
    {
        if (r.schema.contains("\"supported\"")) {
            return QStringLiteral("multi");
        }
        if (r.schema.contains("\"expected_answer\"")) {
            return QStringLiteral("written");
        }
        if (r.schema.contains("\"options\"")) {
            return QStringLiteral("single");
        }
        return QStringLiteral("other");
    }

private:
    void reply(int id, const LlmRequest& request)
    {
        if (!m_pending.contains(id)) {
            return;   // cancelled
        }
        m_pending.removeAll(id);
        const Reply r = responder ? responder(request) : Reply(LlmError{LlmErrorKind::NotRunning, QStringLiteral("no model")});
        if (const auto* text = std::get_if<QString>(&r)) {
            emit finished(id, *text);
        } else {
            emit failed(id, std::get<LlmError>(r));
        }
    }

    int m_nextId = 1;
    QList<int> m_pending;
    QList<QPair<int, LlmRequest>> m_held;
};
