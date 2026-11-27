#include "ui/PixelSegmentedControl.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>

PixelSegmentedControl::PixelSegmentedControl(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
}

int PixelSegmentedControl::addTab(const QString& text)
{
    auto* btn = new QPushButton(text, this);
    btn->setObjectName(QStringLiteral("chip"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setCheckable(false);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int index = static_cast<int>(m_buttons.size());
    m_buttons.append(btn);
    layout()->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, index]() {
        emit tabClicked(index);
        setCurrentIndex(index);
    });
    if (m_current < 0) {
        m_current = 0;
    }
    applyActive();
    return index;
}

void PixelSegmentedControl::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_buttons.size() || index == m_current) {
        return;
    }
    m_current = index;
    applyActive();
    emit currentChanged(index);
}

void PixelSegmentedControl::applyActive()
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        QPushButton* b = m_buttons.at(i);
        b->setProperty("active", i == m_current);
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->setAccessibleName(QStringLiteral("%1 tab%2").arg(b->text(), i == m_current ? QStringLiteral(", selected") : QString()));
    }
}
