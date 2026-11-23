#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>

class StudyTimer;

/**
 * Windows system tray integration for pet visibility and timer controls.
 */
class SystemTrayManager : public QObject {
    Q_OBJECT

public:
    explicit SystemTrayManager(StudyTimer* timer, QObject* parent = nullptr);

    void show();
    void hide();
    [[nodiscard]] bool isVisible() const;

    void notify(const QString& title, const QString& message);

public slots:
    void refreshTimerActions();

signals:
    void showPetRequested();
    void hidePetRequested();
    void startTimerRequested();
    void pauseResumeTimerRequested();
    void openSettingsRequested();
    void exitRequested();

private:
    void buildMenu();

    StudyTimer* m_timer = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_pauseResumeAction = nullptr;
};
