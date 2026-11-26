#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

/**
 * Collapsible pixel-art accordion section with live collapsed summary text,
 * crisp pixel chevron, and smooth 165 ms height transition.
 * Header height is computed from its layout so title and summary never overlap or clip.
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
    [[nodiscard]] QFrame* headerWidget() const { return m_header; }
    [[nodiscard]] QLabel* titleLabel() const { return m_titleLabel; }
    [[nodiscard]] QLabel* summaryLabel() const { return m_summaryLabel; }

signals:
    void toggled(bool expanded);
    void userToggled(bool expanded);
    void heightChanged();

private slots:
    void onHeaderClicked();

private:
    void updateChevron();
    void refreshHeaderVisibility();

    QFrame* m_header = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_chevronLabel = nullptr;
    QWidget* m_bodyContainer = nullptr;
    QPropertyAnimation* m_anim = nullptr;
    bool m_expanded = false;
};
