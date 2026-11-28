#include "studyui/StudySetList.h"

#include "ui/PixelTheme.h"

#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
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

QString StudySetList::statusText(Status status)
{
    switch (status) {
    case Status::Empty:
        return QStringLiteral("No questions yet");
    case Status::Processing:
        return QStringLiteral("Processing…");
    case Status::NeedsAttention:
        return QStringLiteral("Needs attention");
    case Status::Ready:
        return QStringLiteral("Ready");
    }
    return QString();
}

QString StudySetList::statusTone(Status status)
{
    switch (status) {
    case Status::Empty:
        return QString();
    case Status::Processing:
        return QStringLiteral("busy");
    case Status::NeedsAttention:
        return QStringLiteral("attention");
    case Status::Ready:
        return QStringLiteral("ready");
    }
    return QString();
}

QString StudySetList::metaText(qint64 id) const
{
    return m_meta.value(id);
}

QMenu* StudySetList::menuFor(qint64 id)
{
    if (!m_rows.contains(id)) {
        return nullptr;
    }
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("rowMenu"));
    menu->addAction(QStringLiteral("Rename"), this, [this, id]() { emit renameRequested(id); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Delete"), this, [this, id]() { emit deleteRequested(id); });
    return menu;
}

namespace {

/** A small icon followed by a number, e.g. a document glyph and "3". */
QWidget* countBadge(PixelIcon icon, int count, const QString& tooltip, QWidget* parent)
{
    auto* badge = new QWidget(parent);
    auto* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    auto* glyph = new QLabel(badge);
    glyph->setPixmap(PixelTheme::icon(icon, PixelTheme::kSecondaryText, 11).pixmap(11, 11));
    auto* number = new QLabel(QString::number(count), badge);
    number->setObjectName(QStringLiteral("muted"));
    layout->addWidget(glyph);
    layout->addWidget(number);
    badge->setToolTip(tooltip);
    return badge;
}

} // namespace

void StudySetList::setEntries(const QList<Entry>& entries)
{
    for (QFrame* row : std::as_const(m_rows)) {
        m_layout->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    m_rows.clear();
    m_meta.clear();
    m_pills.clear();
    m_menuButtons.clear();
    m_order.clear();

    int insertAt = 0;
    for (const Entry& e : entries) {
        auto* row = new SetRow(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 7, 6, 7);
        layout->setSpacing(6);

        auto* text = new QVBoxLayout();
        text->setSpacing(2);
        auto* name = new QLabel(e.name, row);
        name->setObjectName(QStringLiteral("sectionTitle"));
        name->setTextInteractionFlags(Qt::NoTextInteraction);
        text->addWidget(name);
        auto* counts = new QHBoxLayout();
        counts->setSpacing(10);
        counts->addWidget(countBadge(PixelIcon::Document, e.documents,
                                     QStringLiteral("%1 document%2").arg(e.documents).arg(e.documents == 1 ? QString() : QStringLiteral("s")), row));
        counts->addWidget(countBadge(PixelIcon::Help, e.questions,
                                     QStringLiteral("%1 question%2").arg(e.questions).arg(e.questions == 1 ? QString() : QStringLiteral("s")), row));
        counts->addStretch(1);
        text->addLayout(counts);
        layout->addLayout(text, 1);

        auto* pill = new QLabel(statusText(e.status), row);
        pill->setObjectName(QStringLiteral("statusPill"));
        pill->setProperty("tone", statusTone(e.status));
        layout->addWidget(pill, 0, Qt::AlignVCenter);

        auto* more = new PixelIconButton(PixelIcon::More, 24, 10, false, row);
        more->setToolTip(QStringLiteral("Rename or delete"));
        more->setAccessibleName(QStringLiteral("More actions for %1").arg(e.name));
        layout->addWidget(more, 0, Qt::AlignVCenter);

        const QString meta = summary(e.documents, e.questions);
        row->setToolTip(meta);
        row->setAccessibleName(QStringLiteral("%1, %2, %3").arg(e.name, meta, statusText(e.status)));
        connect(row, &SetRow::clicked, this, [this, id = e.id]() { emit openRequested(id); });
        connect(more, &QPushButton::clicked, this, [this, more, id = e.id]() {
            if (QMenu* menu = menuFor(id)) {
                menu->popup(more->mapToGlobal(QPoint(0, more->height())));
            }
        });

        m_layout->insertWidget(insertAt++, row);
        m_rows.insert(e.id, row);
        m_meta.insert(e.id, meta);
        m_pills.insert(e.id, pill);
        m_menuButtons.insert(e.id, more);
        m_order.append(e.id);
    }
}

#include "StudySetList.moc"
