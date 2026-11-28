#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class PixelIconButton;
class QFrame;
class QLabel;
class QMenu;
class QVBoxLayout;

/**
 * Vertical list of study sets. A row shows the set's name, how many documents and questions it
 * holds (as icons with numbers), and a status pill; clicking the row opens the set, and its
 * "more" menu renames or deletes it. Rows are rebuilt from plain data, so the list never holds
 * database state itself.
 */
class StudySetList : public QWidget {
    Q_OBJECT

public:
    /** What a set needs, shown as a pill on its row. */
    enum class Status {
        Empty,            // no questions yet
        Processing,       // a document is being read or questions are being written
        NeedsAttention,   // a document failed, or questions need context
        Ready
    };

    struct Entry {
        qint64 id = 0;
        QString name;
        int documents = 0;
        int questions = 0;
        Status status = Status::Empty;
    };

    explicit StudySetList(QWidget* parent = nullptr);

    void setEntries(const QList<Entry>& entries);
    [[nodiscard]] QList<qint64> ids() const { return m_order; }
    [[nodiscard]] QFrame* rowFor(qint64 id) const { return m_rows.value(id); }
    /** "3 documents · 12 questions" (the row's accessible summary and tooltip). */
    [[nodiscard]] QString metaText(qint64 id) const;
    [[nodiscard]] QLabel* statusPill(qint64 id) const { return m_pills.value(id); }
    [[nodiscard]] PixelIconButton* menuButton(qint64 id) const { return m_menuButtons.value(id); }
    /** The row's menu (Rename, Delete), built on demand. */
    [[nodiscard]] QMenu* menuFor(qint64 id);

    /** "3 documents · 12 questions"; used for the row subtitle. */
    [[nodiscard]] static QString summary(int documents, int questions);
    [[nodiscard]] static QString statusText(Status status);
    /** Style sheet tone of a status pill: "ready", "busy", "attention" or "" (muted). */
    [[nodiscard]] static QString statusTone(Status status);

signals:
    void openRequested(qint64 id);
    void renameRequested(qint64 id);
    void deleteRequested(qint64 id);

private:
    QVBoxLayout* m_layout = nullptr;
    QHash<qint64, QFrame*> m_rows;
    QHash<qint64, QString> m_meta;
    QHash<qint64, QLabel*> m_pills;
    QHash<qint64, PixelIconButton*> m_menuButtons;
    QList<qint64> m_order;
};
