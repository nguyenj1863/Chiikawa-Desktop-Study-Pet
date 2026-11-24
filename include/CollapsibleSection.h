#pragma once

#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QPushButton;
class QVBoxLayout;

/**
 * Collapsible pixel-art accordion section with live collapsed summary text,
 * crisp pixel chevron, and smooth 160 ms height transition.
 */
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    CollapsibleSection(const QString& title, bool expandedByDefault, QWidget* parent = nullptr);

    void setContentLayout(QLayout* contentLayout);
    void setSummary(const QString& summary);

    [[nodiscard]] bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded, bool animate = true);

    [[nodiscard]] int expectedContentHeight() const;

signals:
    void toggled(bool expanded);
    void heightChanged();

private slots:
    void onHeaderClicked();

private:
    void updateChevron();

    QPushButton* m_headerBtn = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_chevronLabel = nullptr;
    QWidget* m_bodyContainer = nullptr;
    QPropertyAnimation* m_anim = nullptr;
    bool m_expanded = false;
};
