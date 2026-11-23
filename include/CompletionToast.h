#pragma once

#include <QWidget>
#include <QTimer>

class QLabel;
class QPushButton;
class QMovie;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

/**
 * Cute floating study-completion card shown near the pet.
 * Does not steal keyboard focus from other applications.
 */
class CompletionToast : public QWidget {
    Q_OBJECT

public:
    explicit CompletionToast(QWidget* parent = nullptr);
    ~CompletionToast() override;

    void showNear(const QRect& petGlobalGeometry);
    void dismiss();

signals:
    void dismissed();
    void startAgainRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onDismissClicked();
    void onStartAgainClicked();
    void finishFadeOut();

private:
    void clampToScreen(QPoint& pos) const;
    void fadeIn();
    void fadeOut();

    QLabel* m_gifLabel = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QPushButton* m_againBtn = nullptr;
    QMovie* m_movie = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    QTimer m_autoHide;
    bool m_fadingOut = false;
};
