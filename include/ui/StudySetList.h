#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QVBoxLayout;

/**
 * Vertical list of study sets. A row shows the set's name and what it holds; clicking the row
 * opens the set, and the Rename and Delete chips act on that row's set. Rows are rebuilt from
 * plain data, so the list never holds database state itself.
 */
class StudySetList : public QWidget {
    Q_OBJECT

public:
    struct Entry {
        qint64 id = 0;
        QString name;
        int documents = 0;
        int questions = 0;
    };

    explicit StudySetList(QWidget* parent = nullptr);

    void setEntries(const QList<Entry>& entries);
    [[nodiscard]] QList<qint64> ids() const { return m_order; }
    [[nodiscard]] QFrame* rowFor(qint64 id) const { return m_rows.value(id); }
    [[nodiscard]] QString metaText(qint64 id) const;

    /** "3 documents · 12 questions"; used for the row subtitle. */
    [[nodiscard]] static QString summary(int documents, int questions);

signals:
    void openRequested(qint64 id);
    void renameRequested(qint64 id);
    void deleteRequested(qint64 id);

private:
    QVBoxLayout* m_layout = nullptr;
    QHash<qint64, QFrame*> m_rows;
    QHash<qint64, QLabel*> m_meta;
    QList<qint64> m_order;
};
