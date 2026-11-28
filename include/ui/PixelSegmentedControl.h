#pragma once

#include <QList>
#include <QWidget>

class QPushButton;

/** A row of mutually exclusive tab buttons using the shared "chip" look with its active state. */
class PixelSegmentedControl : public QWidget {
    Q_OBJECT

public:
    /** Chip: small tabs inside a form. Nav: a window's top-level pages, in larger type. */
    enum class Variant {
        Chip,
        Nav
    };

    explicit PixelSegmentedControl(QWidget* parent = nullptr);

    void setVariant(Variant variant);
    [[nodiscard]] Variant variant() const { return m_variant; }
    int addTab(const QString& text);
    /** Changes a tab's label (for counts such as "Questions (42)"). */
    void setTabText(int index, const QString& text);
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

    [[nodiscard]] QString buttonObjectName() const;

    QList<QPushButton*> m_buttons;
    int m_current = -1;
    Variant m_variant = Variant::Chip;
};
