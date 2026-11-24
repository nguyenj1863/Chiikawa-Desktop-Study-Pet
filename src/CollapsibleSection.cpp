#include "CollapsibleSection.h"
#include "PixelTheme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace {

/**
 * Clickable, keyboard-accessible header frame whose sizeHint() delegates strictly
 * to its child layout so multi-line title + summary never clips or overlaps.
 */
class SectionHeaderFrame final : public QFrame {
    Q_OBJECT

public:
    explicit SectionHeaderFrame(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("sectionHeader"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        setProperty("hovered", false);
        setProperty("pressed", false);
        setStyleSheet(QStringLiteral(
            "QFrame#sectionHeader {"
            "  background: #FFF0E6;"
            "  border: 2px solid #DDBFB3;"
            "  border-radius: 4px;"
            "}"
            "QFrame#sectionHeader[hovered=\"true\"] {"
            "  background: #F7C2B5;"
            "  border-color: #DF9B92;"
            "}"
            "QFrame#sectionHeader[pressed=\"true\"] {"
            "  background: #F3A6A0;"
            "  border-color: #D28B82;"
            "}"
            "QFrame#sectionHeader:focus {"
            "  border-color: #C97E75;"
            "}"));
    }

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* event) override
    {
        setProperty("hovered", true);
        repolish();
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        setProperty("hovered", false);
        setProperty("pressed", false);
        repolish();
        QFrame::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            setProperty("pressed", true);
            repolish();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const bool inside = rect().contains(event->pos());
            setProperty("pressed", false);
            repolish();
            if (inside) {
                emit clicked();
            }
            event->accept();
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            emit clicked();
            event->accept();
            return;
        }
        QFrame::keyPressEvent(event);
    }

private:
    void repolish()
    {
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
};

} // namespace

CollapsibleSection::CollapsibleSection(const QString& title, bool expandedByDefault, QWidget* parent)
    : QWidget(parent)
    , m_expanded(expandedByDefault)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* headerFrame = new SectionHeaderFrame(this);
    m_header = headerFrame;
    connect(headerFrame, &SectionHeaderFrame::clicked, this, &CollapsibleSection::onHeaderClicked);

    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(10, 7, 10, 7);
    headerLayout->setSpacing(8);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);

    m_titleLabel = new QLabel(title, m_header);
    m_titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_summaryLabel = new QLabel(m_header);
    m_summaryLabel->setObjectName(QStringLiteral("sectionSummary"));
    m_summaryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_summaryLabel->setWordWrap(false);
    m_summaryLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_summaryLabel->setVisible(false);

    textCol->addWidget(m_titleLabel);
    textCol->addWidget(m_summaryLabel);
    headerLayout->addLayout(textCol, 1);

    m_chevronLabel = new QLabel(m_header);
    m_chevronLabel->setFixedSize(14, 14);
    m_chevronLabel->setAlignment(Qt::AlignCenter);
    m_chevronLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_chevronLabel, 0, Qt::AlignVCenter);

    root->addWidget(m_header);

    m_bodyContainer = new QWidget(this);
    m_bodyContainer->setObjectName(QStringLiteral("sectionBody"));
    m_bodyContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_bodyContainer->setMaximumHeight(m_expanded ? 16777215 : 0);
    m_bodyContainer->setVisible(m_expanded);
    root->addWidget(m_bodyContainer);

    m_anim = new QPropertyAnimation(m_bodyContainer, "maximumHeight", this);
    m_anim->setDuration(165);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_anim, &QPropertyAnimation::valueChanged, this, [this]() {
        updateGeometry();
        emit heightChanged();
    });
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_expanded) {
            m_bodyContainer->setVisible(false);
            m_bodyContainer->setMaximumHeight(0);
        } else {
            m_bodyContainer->setMaximumHeight(expectedContentHeight());
        }
        updateGeometry();
        emit heightChanged();
    });

    updateChevron();
}

void CollapsibleSection::setContentLayout(QLayout* contentLayout)
{
    if (!contentLayout) {
        return;
    }
    contentLayout->setContentsMargins(4, 8, 4, 2);
    m_bodyContainer->setLayout(contentLayout);
    if (m_expanded) {
        m_bodyContainer->setVisible(true);
        m_bodyContainer->setMaximumHeight(expectedContentHeight());
    } else {
        m_bodyContainer->setMaximumHeight(0);
        m_bodyContainer->setVisible(false);
    }
    updateGeometry();
}

void CollapsibleSection::setSummary(const QString& summary)
{
    if (m_summaryLabel->text() == summary && m_summaryLabel->isVisible() == (!m_expanded && !summary.isEmpty())) {
        return;
    }
    m_summaryLabel->setText(summary);
    refreshHeaderVisibility();
    emit heightChanged();
}

void CollapsibleSection::refreshHeaderVisibility()
{
    const bool showSummary = !m_expanded && !m_summaryLabel->text().isEmpty();
    m_summaryLabel->setVisible(showSummary);
    if (m_header && m_header->layout()) {
        m_header->layout()->activate();
        m_header->updateGeometry();
    }
    updateGeometry();
}

int CollapsibleSection::expectedContentHeight() const
{
    if (!m_bodyContainer || !m_bodyContainer->layout()) {
        return 0;
    }
    return std::max(0, m_bodyContainer->layout()->sizeHint().height());
}

void CollapsibleSection::setExpanded(bool expanded, bool animate)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    refreshHeaderVisibility();
    updateChevron();

    const int targetH = m_expanded ? expectedContentHeight() : 0;
    m_anim->stop();
    if (!animate) {
        m_bodyContainer->setVisible(m_expanded);
        m_bodyContainer->setMaximumHeight(targetH);
        updateGeometry();
        emit toggled(m_expanded);
        emit heightChanged();
        return;
    }

    if (m_expanded) {
        m_bodyContainer->setMaximumHeight(0);
        m_bodyContainer->setVisible(true);
    }
    m_anim->setStartValue(m_bodyContainer->maximumHeight());
    m_anim->setEndValue(targetH);
    m_anim->start();
    emit toggled(m_expanded);
}

void CollapsibleSection::onHeaderClicked()
{
    setExpanded(!m_expanded, true);
    emit userToggled(m_expanded);
}

void CollapsibleSection::updateChevron()
{
    const PixelIcon kind = m_expanded ? PixelIcon::ChevronDown : PixelIcon::ChevronRight;
    m_chevronLabel->setPixmap(PixelTheme::icon(kind, PixelTheme::kPrimaryText, 12).pixmap(12, 12));
}

#include "CollapsibleSection.moc"
