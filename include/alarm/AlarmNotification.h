#pragma once

#include <QTime>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class PixelIconButton;

/**
 * Floating pixel-art alarm notification. It appears where the timer's completion toast does
 * (bottom-right of the primary screen, via NotificationStack) and stacks above it when both
 * are open. Stays visible until Snooze or Dismiss (or X) is selected; never steals focus.
 */
class AlarmNotification : public QWidget {
    Q_OBJECT

public:
    static constexpr int kFadeInMs = 180;
    static constexpr int kEntranceSlidePx = 6;

    explicit AlarmNotification(QWidget* parent = nullptr);

    /** petGlobalGeometry is kept for callers; the popup goes to the shared notification corner. */
    void showAlarm(const QRect& petGlobalGeometry,
                   const QTime& scheduledTime,
                   int snoozeMinutes,
                   bool fromSnooze);

    void setSnoozeMinutes(int minutes);
    void closeNotification();

signals:
    void snoozeRequested(int minutes);
    void dismissRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSnoozeClicked();
    void onDismissClicked();

private:
    QLabel* m_bellIcon = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_bodyLabel = nullptr;
    PixelIconButton* m_closeBtn = nullptr;
    QPushButton* m_snoozeBtn = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    QPropertyAnimation* m_slide = nullptr;

    int m_snoozeMinutes = 5;
    bool m_actionHandled = false;
};
