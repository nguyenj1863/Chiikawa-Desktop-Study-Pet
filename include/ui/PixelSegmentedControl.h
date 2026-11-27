#pragma once

#include <QList>
#include <QWidget>

class QPushButton;

/** A row of mutually exclusive tab buttons using the shared "chip" look with its active state. */
class PixelSegmentedControl : public QWidget {
    Q_OBJECT

public:
    explicit PixelSegmentedControl(QWidget* parent = nullptr);

    int addTab(const QString& text);
    [[nodiscard]] int currentIndex() const { return m_current; }
    [[nodiscard]] int count() const { return static_cast<int>(m_buttons.size()); }
    [[nodiscard]] QPushButton* button(int index) const { return m_buttons.value(index, nullptr); }
    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);
    /** Emitted when the user clicks a tab (for click sounds), not when set from code. */
    void tabClicked(int index);

private:
    void applyActive();

    QList<QPushButton*> m_buttons;
    int m_current = -1;
};
