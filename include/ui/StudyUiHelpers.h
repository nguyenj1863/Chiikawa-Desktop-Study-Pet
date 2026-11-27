#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>

#include "ui/PixelTheme.h"

/** Small widget factories shared by the Study window and its pages. */
namespace StudyUi {

inline QPushButton* makeButton(const QString& text, QWidget* parent, const char* objectName = nullptr)
{
    auto* btn = new QPushButton(text, parent);
    if (objectName) {
        btn->setObjectName(QString::fromLatin1(objectName));
    }
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

inline QLabel* makeLabel(const QString& text, const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setWordWrap(true);
    return label;
}

inline QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("studyCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    return card;
}

/** Same drop-down list treatment as Settings (one definition in PixelTheme). */
inline void configureTranslucentPopup(QComboBox* combo)
{
    PixelTheme::configureComboPopup(combo);
}

} // namespace StudyUi
