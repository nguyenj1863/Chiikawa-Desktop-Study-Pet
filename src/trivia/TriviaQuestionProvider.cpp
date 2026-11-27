#include "trivia/TriviaQuestionProvider.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace Trivia {

QString rarityName(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return QStringLiteral("Common");
    case Rarity::Uncommon:
        return QStringLiteral("Uncommon");
    case Rarity::Rare:
        return QStringLiteral("Rare");
    case Rarity::Legendary:
        return QStringLiteral("Legendary");
    case Rarity::Incorrect:
        return QStringLiteral("Incorrect");
    }
    return QString();
}

bool parseRarity(const QString& name, Rarity* out)
{
    static const QHash<QString, Rarity> map{
        {QStringLiteral("common"), Rarity::Common},
        {QStringLiteral("uncommon"), Rarity::Uncommon},
        {QStringLiteral("rare"), Rarity::Rare},
        {QStringLiteral("legendary"), Rarity::Legendary},
    };
    const auto it = map.constFind(name.trimmed().toLower());
    if (it == map.constEnd()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

} // namespace Trivia

TriviaQuestionProvider::TriviaQuestionProvider(const QString& jsonPath)
    : m_rng(QRandomGenerator::global()->generate())
{
    QFile file(jsonPath);
    if (file.open(QIODevice::ReadOnly)) {
        m_questions = parse(file.readAll());
    }
}

QVector<Question> TriviaQuestionProvider::parse(const QByteArray& json)
{
    QVector<Question> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return out;
    }
    const QJsonArray items = doc.object().value(QStringLiteral("questions")).toArray();
    QSet<QString> seenIds;
    for (const QJsonValue& value : items) {
        const QJsonObject obj = value.toObject();
        Question q;
        q.id = obj.value(QStringLiteral("id")).toString().trimmed();
        q.prompt = obj.value(QStringLiteral("question")).toString().trimmed();
        q.category = obj.value(QStringLiteral("category")).toString().trimmed();
        q.explanation = obj.value(QStringLiteral("explanation")).toString().trimmed();
        q.providerId = QStringLiteral("trivia");
        const QJsonArray answers = obj.value(QStringLiteral("answers")).toArray();
        for (const QJsonValue& a : answers) {
            const QJsonObject aObj = a.toObject();
            AcceptedAnswer answer;
            answer.canonical = aObj.value(QStringLiteral("answer")).toString().trimmed();
            if (answer.canonical.isEmpty()
                || !Trivia::parseRarity(aObj.value(QStringLiteral("rarity")).toString(), &answer.rarity)) {
                continue;
            }
            const QJsonArray aliases = aObj.value(QStringLiteral("aliases")).toArray();
            for (const QJsonValue& alias : aliases) {
                const QString text = alias.toString().trimmed();
                if (!text.isEmpty()) {
                    answer.aliases.append(text);
                }
            }
            q.answers.append(answer);
        }
        if (!q.isValid() || seenIds.contains(q.id)) {
            continue;
        }
        seenIds.insert(q.id);
        out.append(q);
    }
    return out;
}

std::optional<Question> TriviaQuestionProvider::nextQuestion(const QStringList& recentIds)
{
    if (m_questions.isEmpty()) {
        return std::nullopt;
    }
    QVector<qsizetype> fresh;
    for (qsizetype i = 0; i < m_questions.size(); ++i) {
        if (!recentIds.contains(m_questions.at(i).id)) {
            fresh.append(i);
        }
    }
    if (!fresh.isEmpty()) {
        const auto pick = static_cast<qsizetype>(m_rng.bounded(static_cast<quint32>(fresh.size())));
        return m_questions.at(fresh.at(pick));
    }
    // Every question was asked recently: reuse the one asked longest ago (front of the list).
    for (const QString& oldest : recentIds) {
        for (const Question& q : m_questions) {
            if (q.id == oldest) {
                return q;
            }
        }
    }
    return m_questions.first();
}
