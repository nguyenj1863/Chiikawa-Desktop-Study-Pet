#pragma once

#include <QTime>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class PixelIconButton;

/**
 * Floating kawaii pixel-art alarm notification positioned near Chiikawa.
 * Stays visible until Snooze or Dismiss (or X) is selected; never steals focus.
 */
class AlarmNotification : public QWidget {
    Q_OBJECT

public:
    explicit AlarmNotification(QWidget* parent = nullptr);

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
    void clampToScreen(QPoint& pos) const;

    QLabel* m_bellIcon = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_bodyLabel = nullptr;
    PixelIconButton* m_closeBtn = nullptr;
    QPushButton* m_snoozeBtn = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;

    int m_snoozeMinutes = 5;
    bool m_actionHandled = false;
};
