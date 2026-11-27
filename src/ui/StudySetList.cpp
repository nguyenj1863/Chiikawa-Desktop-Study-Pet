#include "ui/StudySetList.h"

#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/** A card that reports clicks anywhere on it except on its own buttons. */
class SetRow : public QFrame {
    Q_OBJECT

public:
    explicit SetRow(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("setRow"));
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        applyStyle(false);
    }

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* event) override
    {
        applyStyle(true);
        QFrame::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        applyStyle(false);
        QFrame::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            event->accept();   // keeps the window from treating the press as a drag
            return;
        }
        QFrame::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            emit clicked();
        }
        QFrame::mouseReleaseEvent(event);
    }

private:
    void applyStyle(bool hover)
    {
        setStyleSheet(hover ? QStringLiteral("QFrame#setRow { background: rgba(255, 255, 255, 26);"
                                             " border: 1px solid rgba(138, 185, 255, 80); border-radius: 10px; }")
                            : QStringLiteral("QFrame#setRow { background: rgba(20, 24, 32, 68);"
                                             " border: 1px solid rgba(255, 255, 255, 10); border-radius: 10px; }"));
    }
};

} // namespace

StudySetList::StudySetList(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("setList"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 4, 0);
    m_layout->setSpacing(5);
    m_layout->addStretch(1);
}

QString StudySetList::summary(int documents, int questions)
{
    return QStringLiteral("%1 document%2 · %3 question%4")
        .arg(documents)
        .arg(documents == 1 ? QString() : QStringLiteral("s"))
        .arg(questions)
        .arg(questions == 1 ? QString() : QStringLiteral("s"));
}

QString StudySetList::metaText(qint64 id) const
{
    const QLabel* label = m_meta.value(id);
    return label ? label->text() : QString();
}

void StudySetList::setEntries(const QList<Entry>& entries)
{
    for (QFrame* row : std::as_const(m_rows)) {
        m_layout->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    m_rows.clear();
    m_meta.clear();
    m_order.clear();

    int insertAt = 0;
    for (const Entry& e : entries) {
        auto* row = new SetRow(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 7, 8, 7);
        layout->setSpacing(6);

        auto* text = new QVBoxLayout();
        text->setSpacing(1);
        auto* name = new QLabel(e.name, row);
        name->setObjectName(QStringLiteral("sectionTitle"));
        name->setTextInteractionFlags(Qt::NoTextInteraction);
        auto* meta = new QLabel(summary(e.documents, e.questions), row);
        meta->setObjectName(QStringLiteral("muted"));
        text->addWidget(name);
        text->addWidget(meta);
        layout->addLayout(text, 1);

        auto* rename = new QPushButton(QStringLiteral("Rename"), row);
        rename->setObjectName(QStringLiteral("chip"));
        rename->setCursor(Qt::PointingHandCursor);
        rename->setAccessibleName(QStringLiteral("Rename %1").arg(e.name));
        auto* remove = new QPushButton(QStringLiteral("Delete"), row);
        remove->setObjectName(QStringLiteral("chip"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setAccessibleName(QStringLiteral("Delete %1").arg(e.name));
        auto* arrow = new QLabel(QStringLiteral("›"), row);
        arrow->setObjectName(QStringLiteral("sectionTitle"));
        layout->addWidget(rename);
        layout->addWidget(remove);
        layout->addWidget(arrow);

        connect(row, &SetRow::clicked, this, [this, id = e.id]() { emit openRequested(id); });
        connect(rename, &QPushButton::clicked, this, [this, id = e.id]() { emit renameRequested(id); });
        connect(remove, &QPushButton::clicked, this, [this, id = e.id]() { emit deleteRequested(id); });

        m_layout->insertWidget(insertAt++, row);
        m_rows.insert(e.id, row);
        m_meta.insert(e.id, meta);
        m_order.append(e.id);
    }
}

#include "StudySetList.moc"
