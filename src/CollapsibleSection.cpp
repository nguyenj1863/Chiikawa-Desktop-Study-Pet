#include "CollapsibleSection.h"
#include "PixelTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>

CollapsibleSection::CollapsibleSection(const QString& title, bool expandedByDefault, QWidget* parent)
    : QWidget(parent)
    , m_expanded(expandedByDefault)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_headerBtn = new QPushButton(this);
    m_headerBtn->setFlat(true);
    m_headerBtn->setCursor(Qt::PointingHandCursor);
    m_headerBtn->setFocusPolicy(Qt::StrongFocus);
    m_headerBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_headerBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #FFF0E6;"
        "  border: 2px solid #DDBFB3;"
        "  border-radius: 4px;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "QPushButton:hover {"
        "  background: #F7C2B5;"
        "  border-color: #DF9B92;"
        "}"
        "QPushButton:pressed {"
        "  background: #F3A6A0;"
        "}"));

    auto* headerLayout = new QHBoxLayout(m_headerBtn);
    headerLayout->setContentsMargins(10, 7, 10, 7);
    headerLayout->setSpacing(6);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(1);

    m_titleLabel = new QLabel(title, m_headerBtn);
    m_titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_summaryLabel = new QLabel(m_headerBtn);
    m_summaryLabel->setObjectName(QStringLiteral("sectionSummary"));
    m_summaryLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_summaryLabel->setVisible(false);

    textCol->addWidget(m_titleLabel);
    textCol->addWidget(m_summaryLabel);
    headerLayout->addLayout(textCol, 1);

    m_chevronLabel = new QLabel(m_headerBtn);
    m_chevronLabel->setFixedSize(14, 14);
    m_chevronLabel->setAlignment(Qt::AlignCenter);
    m_chevronLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_chevronLabel, 0, Qt::AlignVCenter);

    connect(m_headerBtn, &QPushButton::clicked, this, &CollapsibleSection::onHeaderClicked);
    root->addWidget(m_headerBtn);

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
        emit heightChanged();
    });
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_expanded) {
            m_bodyContainer->setVisible(false);
            m_bodyContainer->setMaximumHeight(0);
        } else {
            m_bodyContainer->setMaximumHeight(expectedContentHeight());
        }
        emit heightChanged();
    });

    updateChevron();
}

void CollapsibleSection::setContentLayout(QLayout* contentLayout)
{
    if (!contentLayout) {
        return;
    }
    contentLayout->setContentsMargins(4, 10, 4, 4);
    m_bodyContainer->setLayout(contentLayout);
    if (m_expanded) {
        m_bodyContainer->setVisible(true);
        m_bodyContainer->setMaximumHeight(expectedContentHeight());
    } else {
        m_bodyContainer->setMaximumHeight(0);
        m_bodyContainer->setVisible(false);
    }
}

void CollapsibleSection::setSummary(const QString& summary)
{
    m_summaryLabel->setText(summary);
    m_summaryLabel->setVisible(!m_expanded && !summary.isEmpty());
    m_headerBtn->setFixedHeight(m_headerBtn->sizeHint().height());
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
    m_summaryLabel->setVisible(!m_expanded && !m_summaryLabel->text().isEmpty());
    m_headerBtn->setFixedHeight(m_headerBtn->sizeHint().height());
    updateChevron();

    const int targetH = m_expanded ? expectedContentHeight() : 0;
    m_anim->stop();
    if (!animate) {
        m_bodyContainer->setVisible(m_expanded);
        m_bodyContainer->setMaximumHeight(targetH);
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
}

void CollapsibleSection::updateChevron()
{
    const PixelIcon kind = m_expanded ? PixelIcon::ChevronDown : PixelIcon::ChevronRight;
    m_chevronLabel->setPixmap(PixelTheme::icon(kind, PixelTheme::kPrimaryText, 12).pixmap(12, 12));
}
