#include "SystemTrayManager.h"
#include "StudyTimer.h"

#include <QApplication>
#include <QStyle>

SystemTrayManager::SystemTrayManager(StudyTimer* timer, QObject* parent)
    : QObject(parent)
    , m_timer(timer)
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    m_tray->setToolTip(QStringLiteral("Chiikawa Study Pet"));

    buildMenu();
    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            emit showPetRequested();
        }
    });

    if (m_timer) {
        connect(m_timer, &StudyTimer::stateChanged, this, [this](StudyTimer::State) {
            refreshTimerActions();
        });
    }
}

void SystemTrayManager::buildMenu()
{
    m_menu = new QMenu();

    auto* showPet = m_menu->addAction(QStringLiteral("Show Pet"));
    connect(showPet, &QAction::triggered, this, &SystemTrayManager::showPetRequested);

    auto* hidePet = m_menu->addAction(QStringLiteral("Hide Pet"));
    connect(hidePet, &QAction::triggered, this, &SystemTrayManager::hidePetRequested);

    m_menu->addSeparator();

    auto* startTimer = m_menu->addAction(QStringLiteral("Start Study Timer"));
    connect(startTimer, &QAction::triggered, this, &SystemTrayManager::startTimerRequested);

    m_pauseResumeAction = m_menu->addAction(QStringLiteral("Pause Timer"));
    connect(m_pauseResumeAction, &QAction::triggered, this, &SystemTrayManager::pauseResumeTimerRequested);

    m_menu->addSeparator();

    auto* settings = m_menu->addAction(QStringLiteral("Open Settings"));
    connect(settings, &QAction::triggered, this, &SystemTrayManager::openSettingsRequested);

    m_menu->addSeparator();

    auto* exitAct = m_menu->addAction(QStringLiteral("Exit"));
    connect(exitAct, &QAction::triggered, this, &SystemTrayManager::exitRequested);

    refreshTimerActions();
}

void SystemTrayManager::refreshTimerActions()
{
    if (!m_pauseResumeAction || !m_timer) {
        return;
    }
    if (m_timer->isPaused()) {
        m_pauseResumeAction->setText(QStringLiteral("Resume Timer"));
        m_pauseResumeAction->setEnabled(true);
    } else if (m_timer->isRunning()) {
        m_pauseResumeAction->setText(QStringLiteral("Pause Timer"));
        m_pauseResumeAction->setEnabled(true);
    } else {
        m_pauseResumeAction->setText(QStringLiteral("Pause / Resume Timer"));
        m_pauseResumeAction->setEnabled(false);
    }
}

void SystemTrayManager::show()
{
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray->show();
    }
}

void SystemTrayManager::hide()
{
    m_tray->hide();
}

bool SystemTrayManager::isVisible() const
{
    return m_tray && m_tray->isVisible();
}

void SystemTrayManager::notify(const QString& title, const QString& message)
{
    if (m_tray && m_tray->isVisible()) {
        m_tray->showMessage(title, message, QSystemTrayIcon::Information, 4000);
    }
}
