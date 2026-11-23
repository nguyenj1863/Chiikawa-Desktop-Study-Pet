#pragma once

#include <QWidget>
#include <QLabel>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

/**
 * Soft floating countdown bubble shown while hovering the pet.
 * Never steals keyboard focus; updates label text only when needed.
 */
class TimerBubble : public QWidget {
    Q_OBJECT

public:
    explicit TimerBubble(QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry);
    void updateText(const QString& text);
    void fadeIn();
    void fadeOut();

    [[nodiscard]] bool isFadingOut() const { return m_fadingOut; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void reposition(const QRect& petGlobalGeometry);

    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    bool m_fadingOut = false;
    QString m_lastText;
};
