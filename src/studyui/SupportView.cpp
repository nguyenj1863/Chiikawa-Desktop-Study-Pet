#include "studyui/SupportView.h"

#include "ui/PixelTheme.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

// --- SupportImage -------------------------------------------------------------

SupportImage::SupportImage(const QPixmap& pixmap, QWidget* parent)
    : QWidget(parent)
    , m_pixmap(pixmap)
{
    setObjectName(QStringLiteral("supportImage"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to enlarge"));
    setAccessibleName(QStringLiteral("Figure for this question"));
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

int SupportImage::heightForWidth(int width) const
{
    if (m_pixmap.isNull() || m_pixmap.width() <= 0) {
        return 0;
    }
    const int natural = qRound(static_cast<double>(width) * m_pixmap.height() / m_pixmap.width());
    return std::min(kMaxHeight, natural);
}

QSize SupportImage::sizeHint() const
{
    const int w = std::min(280, m_pixmap.width());
    return {w, heightForWidth(w)};
}

void SupportImage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_pixmap.isNull()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QSize scaled = m_pixmap.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
    // A light card behind the figure keeps dark-on-transparent diagrams readable on the dark glass.
    QPainterPath frame;
    frame.addRoundedRect(QRectF(target).adjusted(-0.5, -0.5, 0.5, 0.5), 6, 6);
    p.fillPath(frame, QColor(0xF5, 0xF7, 0xFA));
    p.setClipPath(frame);
    p.drawPixmap(target, m_pixmap);
}

void SupportImage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    QWidget::mouseReleaseEvent(event);
}

// --- SupportImageViewer -------------------------------------------------------

SupportImageViewer::SupportImageViewer(const QPixmap& pixmap, const QString& caption, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_pixmap(pixmap)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setWindowTitle(caption.isEmpty() ? QStringLiteral("Figure") : caption);
    setAccessibleName(windowTitle());
    QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    const QSize target = pixmap.size().scaled(avail.size() * 0.85, Qt::KeepAspectRatio).boundedTo(pixmap.size() * 2);
    resize(target + QSize(24, 24));
    move(avail.center() - rect().center());
}

void SupportImageViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    PixelTheme::paintGlassSurface(p, rect(), PixelTheme::GlassLevel::Primary, PixelTheme::kRadiusWindow, 4);
    const QRect inner = rect().adjusted(12, 12, -12, -12);
    const QSize scaled = m_pixmap.size().scaled(inner.size(), Qt::KeepAspectRatio);
    const QRect target(inner.topLeft() + QPoint((inner.width() - scaled.width()) / 2, (inner.height() - scaled.height()) / 2), scaled);
    p.fillRect(target, QColor(0xF5, 0xF7, 0xFA));
    p.drawPixmap(target, m_pixmap);
}

void SupportImageViewer::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    accept();
}

// --- SupportView -----------------------------------------------------------------

SupportView::SupportView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("quizBody"));   // transparent like the rest of the question body
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 2, 0, 2);
    m_layout->setSpacing(4);
    hide();
}

namespace {

/**
 * Holds a table at its natural width: cells never squeeze into unreadable columns; a table wider
 * than the bubble scrolls sideways inside its own area while the question stays put.
 */
class SupportTableScroll : public QScrollArea {
public:
    SupportTableScroll(QLabel* table, QWidget* parent)
        : QScrollArea(parent)
        , m_table(table)
    {
        setObjectName(QStringLiteral("supportTableScroll"));
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(false);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport()->setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setWidget(table);
        table->adjustSize();
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override
    {
        const QSize natural = m_table->sizeHint();
        return natural.height() + (natural.width() > width ? horizontalScrollBar()->sizeHint().height() : 0);
    }
    [[nodiscard]] QSize sizeHint() const override
    {
        const QSize natural = m_table->sizeHint();
        return {std::min(natural.width(), 300), natural.height()};
    }
    [[nodiscard]] QSize minimumSizeHint() const override { return {60, m_table->sizeHint().height()}; }

private:
    QLabel* m_table = nullptr;
};

} // namespace

QString SupportView::captionFor(const SupportRef& ref, const QString& location)
{
    const QString caption = ref.caption.trimmed();
    if (location.isEmpty()) {
        return caption;
    }
    // A whole-page image is captioned "Page 4" or "Page 4 (scanned)": the location already says that.
    static const QRegularExpression pageOnly(QStringLiteral(R"(^Page \d+( \(scanned\))?$)"));
    if (caption.isEmpty() || pageOnly.match(caption).hasMatch()) {
        return QStringLiteral("From %1").arg(location);
    }
    return caption.contains(location, Qt::CaseInsensitive) ? caption : QStringLiteral("%1 · %2").arg(caption, location);
}

QString SupportView::tableHtml(const TableData& table)
{
    QString html = QStringLiteral(
        "<table cellspacing=\"0\" cellpadding=\"5\" border=\"1\" style=\"border-color:#4A5465; border-style:solid;\">");
    const auto row = [](const QStringList& cells, bool header) {
        QString r = QStringLiteral("<tr>");
        for (const QString& c : cells) {
            // Non-breaking padding on both sides gives cells room without a stylesheet font rule.
            r += header ? QStringLiteral("<th align=\"left\" style=\"color:#F3F6FC;\">&nbsp;%1&nbsp;</th>").arg(c.toHtmlEscaped())
                        : QStringLiteral("<td style=\"color:#DCE3EE;\">&nbsp;%1&nbsp;</td>").arg(c.toHtmlEscaped());
        }
        return r + QStringLiteral("</tr>");
    };
    if (!table.headers.isEmpty()) {
        html += row(table.headers, true);
    }
    for (const QStringList& r : table.rows) {
        html += row(r, false);
    }
    return html + QStringLiteral("</table>");
}

void SupportView::setSupport(const QVector<SupportRef>& support, const QString& location)
{
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    m_count = 0;
    for (const SupportRef& ref : support) {
        QWidget* shown = nullptr;
        switch (ref.kind) {
        case SupportKind::Image: {
            const QPixmap pixmap(ref.imagePath);
            if (pixmap.isNull()) {
                break;
            }
            auto* image = new SupportImage(pixmap, this);
            const QString caption = ref.caption;
            connect(image, &SupportImage::clicked, this, [this, pixmap, caption]() {
                emit imageOpened();
                SupportImageViewer viewer(pixmap, caption, window());
                viewer.exec();
            });
            shown = image;
            break;
        }
        case SupportKind::Table: {
            if (ref.table.isEmpty()) {
                break;
            }
            auto* table = new QLabel();
            table->setObjectName(QStringLiteral("supportTable"));
            table->setTextFormat(Qt::RichText);
            table->setWordWrap(false);
            table->setText(tableHtml(ref.table));
            table->setAccessibleName(QStringLiteral("Table for this question"));
            shown = new SupportTableScroll(table, this);
            break;
        }
        case SupportKind::Data: {
            if (ref.text.trimmed().isEmpty()) {
                break;
            }
            auto* snippet = new QLabel(ref.text, this);
            snippet->setObjectName(QStringLiteral("supportData"));
            snippet->setWordWrap(true);
            snippet->setTextFormat(Qt::PlainText);
            snippet->setAccessibleName(QStringLiteral("Data for this question"));
            shown = snippet;
            break;
        }
        }
        if (!shown) {
            continue;
        }
        m_layout->addWidget(shown);
        if (const QString text = captionFor(ref, location); !text.isEmpty()) {
            auto* caption = new QLabel(text, this);
            caption->setObjectName(QStringLiteral("muted"));
            caption->setProperty("supportCaption", true);
            caption->setWordWrap(true);
            caption->setTextFormat(Qt::PlainText);
            m_layout->addWidget(caption);
        }
        ++m_count;
    }
    setVisible(m_count > 0);
}

QList<SupportImage*> SupportView::images() const
{
    return findChildren<SupportImage*>();
}

QList<QLabel*> SupportView::tables() const
{
    return findChildren<QLabel*>(QStringLiteral("supportTable"));
}

QList<QLabel*> SupportView::captions() const
{
    QList<QLabel*> out;
    for (QLabel* l : findChildren<QLabel*>()) {
        if (l->property("supportCaption").toBool()) {
            out.append(l);
        }
    }
    return out;
}
