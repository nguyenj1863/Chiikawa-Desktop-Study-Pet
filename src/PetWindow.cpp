#include "PetWindow.h"

#include "AnimationManager.h"
#include "AppSettings.h"
#include "MovementController.h"
#include "SettingsPanel.h"
#include "StudyTimer.h"
#include "SystemTrayManager.h"
#include "TimerBubble.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

PetWindow::PetWindow(QWidget* parent)
    : QWidget(parent)
{
    m_settings = std::make_unique<AppSettings>();
    m_timer = std::make_unique<StudyTimer>(this);

    setupWindowFlags();
    setupUi();

    m_animations = std::make_unique<AnimationManager>(m_petLabel, this);
    m_animations->setTargetSize(m_settings->petSize());
    m_animations->setAutoSwitchEnabled(m_settings->autoAnimationEnabled());
    if (!m_animations->loadAll()) {
        m_petLabel->setText(QStringLiteral("GIF?"));
    }

    m_movement = std::make_unique<MovementController>(this, this);
    m_movement->setEnabled(m_settings->movementEnabled());
    m_movement->setSpeedMultiplier(m_settings->movementSpeed());

    m_bubble = std::make_unique<TimerBubble>(nullptr);
    m_settingsPanel = std::make_unique<SettingsPanel>(m_settings.get(), m_timer.get(), nullptr);

    m_tray = std::make_unique<SystemTrayManager>(m_timer.get(), this);
    m_tray->show();

    connect(m_movement.get(), &MovementController::facingChanged,
            this, &PetWindow::onFacingChanged);
    connect(m_movement.get(), &MovementController::moved,
            this, &PetWindow::onPositionMoved);

    connect(m_timer.get(), &StudyTimer::remainingChanged,
            this, &PetWindow::onTimerRemaining);
    connect(m_timer.get(), &StudyTimer::completed,
            this, &PetWindow::onTimerCompleted);
    connect(m_timer.get(), &StudyTimer::stateChanged, this, [this](StudyTimer::State state) {
        if (state == StudyTimer::State::Running) {
            m_completionHandled = false;
        }
    });

    connect(m_settingsPanel.get(), &SettingsPanel::settingsApplied,
            this, &PetWindow::applySettings);
    connect(m_settingsPanel.get(), &SettingsPanel::exitApplicationRequested,
            this, &PetWindow::exitApplication);
    connect(m_settingsPanel.get(), &SettingsPanel::closedByUser, this, [this]() {
        if (m_settings->movementEnabled()) {
            m_movement->resume();
        }
    });

    connect(m_tray.get(), &SystemTrayManager::showPetRequested, this, &PetWindow::showPet);
    connect(m_tray.get(), &SystemTrayManager::hidePetRequested, this, &PetWindow::hidePet);
    connect(m_tray.get(), &SystemTrayManager::startTimerRequested, this, &PetWindow::startTimerFromTray);
    connect(m_tray.get(), &SystemTrayManager::pauseResumeTimerRequested, this, &PetWindow::pauseResumeFromTray);
    connect(m_tray.get(), &SystemTrayManager::openSettingsRequested, this, &PetWindow::openSettings);
    connect(m_tray.get(), &SystemTrayManager::exitRequested, this, &PetWindow::exitApplication);

    m_gearHideTimer.setSingleShot(true);
    connect(&m_gearHideTimer, &QTimer::timeout, this, &PetWindow::hideGear);

    m_singleClickTimer.setSingleShot(true);
    connect(&m_singleClickTimer, &QTimer::timeout, this, [this]() {
        if (m_animations) {
            m_animations->playRandom(true);
        }
    });

    m_completionHideTimer.setSingleShot(true);
    connect(&m_completionHideTimer, &QTimer::timeout, this, [this]() {
        if (m_completionBanner) {
            m_completionBanner->hide();
        }
    });

    placeInitialPosition();
    resize(m_settings->petSize(), m_settings->petSize());
    applySettings();
}

PetWindow::~PetWindow()
{
    if (m_settings) {
        m_settings->setLastPosition(pos());
        m_settings->save();
    }
}

void PetWindow::setupWindowFlags()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_settings->alwaysOnTop()) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
}

void PetWindow::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_petLabel = new QLabel(this);
    m_petLabel->setAlignment(Qt::AlignCenter);
    m_petLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_petLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(m_petLabel);

    m_gearButton = new QPushButton(QStringLiteral("⚙"), this);
    m_gearButton->setFixedSize(28, 28);
    m_gearButton->setCursor(Qt::PointingHandCursor);
    m_gearButton->setToolTip(QStringLiteral("Settings"));
    m_gearButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: rgba(255, 246, 238, 230);"
        "  color: #6a5545;"
        "  border: 1px solid rgba(220, 200, 180, 180);"
        "  border-radius: 14px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover { background: rgba(255, 236, 220, 245); }"));
    m_gearOpacity = new QGraphicsOpacityEffect(m_gearButton);
    m_gearOpacity->setOpacity(0.0);
    m_gearButton->setGraphicsEffect(m_gearOpacity);
    m_gearFade = new QPropertyAnimation(m_gearOpacity, "opacity", this);
    m_gearFade->setDuration(200);
    m_gearButton->hide();
    connect(m_gearButton, &QPushButton::clicked, this, &PetWindow::onGearClicked);

    m_completionBanner = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_completionBanner->setAttribute(Qt::WA_TranslucentBackground);
    m_completionBanner->setAttribute(Qt::WA_ShowWithoutActivating);
    m_completionBanner->setFocusPolicy(Qt::NoFocus);
    auto* bannerLayout = new QHBoxLayout(m_completionBanner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    m_completionLabel = new QLabel(QStringLiteral("Study session complete!"), m_completionBanner);
    m_completionLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #5c4a3a;"
        "  background: rgba(255, 250, 242, 235);"
        "  border: 1px solid rgba(220, 200, 180, 180);"
        "  border-radius: 12px;"
        "  padding: 8px 14px;"
        "  font-weight: 600;"
        "  font-size: 13px;"
        "}"));
    bannerLayout->addWidget(m_completionLabel);
    m_completionBanner->hide();
}

QPoint PetWindow::defaultBottomRight() const
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {100, 100};
    }
    const QRect avail = screen->availableGeometry();
    const int size = m_settings ? m_settings->petSize() : 120;
    return {avail.right() - size - 24, avail.bottom() - size - 24};
}

bool PetWindow::isSavedPositionSafe(const QPoint& pos) const
{
    const QRect probe(pos, QSize(m_settings->petSize(), m_settings->petSize()));
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (screen->availableGeometry().intersects(probe)) {
            return true;
        }
    }
    return false;
}

void PetWindow::placeInitialPosition()
{
    if (m_settings->hasSavedPosition() && isSavedPositionSafe(m_settings->lastPosition())) {
        move(m_settings->lastPosition());
    } else {
        move(defaultBottomRight());
    }
}

void PetWindow::applySettings()
{
    const int size = m_settings->petSize();
    resize(size, size);
    if (m_animations) {
        m_animations->setTargetSize(size);
        m_animations->setAutoSwitchEnabled(m_settings->autoAnimationEnabled());
    }
    if (m_movement) {
        m_movement->setSpeedMultiplier(m_settings->movementSpeed());
        m_movement->setEnabled(m_settings->movementEnabled());
        if (!m_settingsPanel->isVisible() && !m_dragging) {
            if (m_settings->movementEnabled()) {
                m_movement->resume();
            } else {
                m_movement->pause();
            }
        }
    }

    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_settings->alwaysOnTop()) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    const bool visible = isVisible();
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    if (visible) {
        show();
    }

    if (m_gearButton) {
        m_gearButton->move(width() - m_gearButton->width() - 2, 2);
    }
    m_movement->constrainToScreen();
    updateHitMask();
}

void PetWindow::updateHitMask()
{
    // Approximate opaque hit area: full widget. Fine-grained alpha hit-testing
    // is handled in nativeEvent on Windows when a pixmap is available.
    clearMask();
}

void PetWindow::showPet()
{
    show();
    raise();
}

void PetWindow::hidePet()
{
    hide();
    if (m_bubble) {
        m_bubble->hide();
    }
}

void PetWindow::openSettings()
{
    if (m_movement) {
        m_movement->pause();
    }
    hideGear();
    m_settingsPanel->showNear(frameGeometry());
}

void PetWindow::startTimerFromTray()
{
    m_completionHandled = false;
    m_timer->configure(std::chrono::seconds{m_settings->lastTimerSeconds()});
    m_timer->start();
}

void PetWindow::pauseResumeFromTray()
{
    if (m_timer->isRunning()) {
        m_timer->pause();
    } else if (m_timer->isPaused()) {
        m_timer->resume();
    }
}

void PetWindow::exitApplication()
{
    m_settings->setLastPosition(pos());
    m_settings->save();
    if (m_tray) {
        m_tray->hide();
    }
    QApplication::quit();
}

void PetWindow::onFacingChanged(bool facingLeft)
{
    if (m_animations) {
        m_animations->setMirrored(!facingLeft);
    }
}

void PetWindow::onPositionMoved(const QPoint& pos)
{
    m_settings->setLastPosition(pos);
    if (m_gearButton && m_gearButton->isVisible()) {
        m_gearButton->move(width() - m_gearButton->width() - 2, 2);
    }
    if (m_hovering && m_timer->isActive()) {
        updateHoverBubble();
    }
}

void PetWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragMoved = false;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        if (m_movement) {
            m_movement->pause();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint newPos = event->globalPosition().toPoint() - m_dragOffset;
        move(newPos);
        if (m_movement) {
            m_movement->constrainToScreen();
        }
        m_dragMoved = true;
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_settings->setLastPosition(pos());
        m_settings->save();

        if (!m_dragMoved) {
            // Defer single-click so a double-click can cancel it
            m_singleClickTimer.start(QApplication::doubleClickInterval());
        }

        if (m_settings->movementEnabled() && !m_settingsPanel->isVisible()) {
            m_movement->resume();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_singleClickTimer.stop();
        showGear();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::showGear()
{
    m_gearButton->move(width() - m_gearButton->width() - 2, 2);
    m_gearButton->show();
    m_gearButton->raise();
    m_gearFade->stop();
    m_gearFade->setStartValue(m_gearOpacity->opacity());
    m_gearFade->setEndValue(1.0);
    m_gearFade->start();
    m_gearHideTimer.start(5000);
}

void PetWindow::hideGear()
{
    m_gearHideTimer.stop();
    m_gearFade->stop();
    m_gearFade->setStartValue(m_gearOpacity->opacity());
    m_gearFade->setEndValue(0.0);
    connect(m_gearFade, &QPropertyAnimation::finished, this, [this]() {
        if (m_gearOpacity && m_gearOpacity->opacity() < 0.05) {
            m_gearButton->hide();
        }
    }, Qt::SingleShotConnection);
    m_gearFade->start();
}

void PetWindow::onGearClicked()
{
    m_gearHideTimer.stop();
    openSettings();
}

void PetWindow::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_hovering = true;
    if (m_gearButton->isVisible()) {
        m_gearHideTimer.start(5000);
    }
    updateHoverBubble();
}

void PetWindow::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_hovering = false;
    if (m_bubble) {
        m_bubble->fadeOut();
    }
}

void PetWindow::updateHoverBubble()
{
    if (!m_hovering || !m_timer->isActive()) {
        if (m_bubble && m_bubble->isVisible()) {
            m_bubble->fadeOut();
        }
        return;
    }

    QString text;
    const auto rem = m_timer->remaining();
    if (m_timer->isPaused()) {
        text = QStringLiteral("Paused · %1").arg(StudyTimer::formatRemaining(rem));
    } else {
        text = QStringLiteral("%1 remaining").arg(StudyTimer::formatRemaining(rem));
    }
    m_bubble->updateText(text);
    m_bubble->showNear(frameGeometry());
}

void PetWindow::onTimerRemaining(std::chrono::milliseconds remaining)
{
    Q_UNUSED(remaining);
    if (m_hovering) {
        updateHoverBubble();
    }
}

void PetWindow::playCompletionFeedback()
{
    if (!m_settings->soundEnabled()) {
        return;
    }
    // Gentle system beep — no external audio asset required
#ifdef Q_OS_WIN
    MessageBeep(MB_OK);
#endif
}

void PetWindow::flashCompletionBanner()
{
    if (!m_completionBanner) {
        return;
    }
    m_completionLabel->setText(QStringLiteral("Study session complete!"));
    m_completionBanner->adjustSize();
    const QRect pet = frameGeometry();
    QPoint pos(pet.center().x() - m_completionBanner->width() / 2, pet.top() - m_completionBanner->height() - 12);
    if (QScreen* screen = QGuiApplication::screenAt(pet.center())) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), avail.right() - m_completionBanner->width() + 1));
        pos.setY(std::clamp(pos.y(), avail.top(), avail.bottom() - m_completionBanner->height() + 1));
    }
    m_completionBanner->move(pos);
    m_completionBanner->show();
    m_completionBanner->raise();
    m_completionHideTimer.start(5000);
}

void PetWindow::onTimerCompleted()
{
    if (m_completionHandled) {
        return;
    }
    m_completionHandled = true;

    if (m_animations) {
        m_animations->playCelebration();
    }
    flashCompletionBanner();
    playCompletionFeedback();
    if (m_tray) {
        m_tray->notify(QStringLiteral("Chiikawa Study Pet"),
                       QStringLiteral("Study session complete!"));
    }
    if (m_bubble && m_hovering) {
        m_bubble->updateText(QStringLiteral("Done!"));
    }
}

void PetWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_gearButton) {
        m_gearButton->move(width() - m_gearButton->width() - 2, 2);
    }
    updateHitMask();
}

bool PetWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            // Alpha-aware hit testing: transparent pixels let clicks pass through
            const qreal dpr = devicePixelRatioF();
            const int x = static_cast<int>(static_cast<short>(LOWORD(msg->lParam)));
            const int y = static_cast<int>(static_cast<short>(HIWORD(msg->lParam)));
            const QPoint local = mapFromGlobal(QPoint(x, y));

            if (!rect().contains(local)) {
                *result = HTTRANSPARENT;
                return true;
            }

            if (m_gearButton && m_gearButton->isVisible() && m_gearButton->geometry().contains(local)) {
                *result = HTCLIENT;
                return true;
            }

            if (m_petLabel) {
                const QPixmap px = m_petLabel->pixmap();
                if (!px.isNull()) {
                    const QSize labelSize = m_petLabel->size();
                    const QPixmap scaled = px.scaled(labelSize * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    const int ox = (labelSize.width() - scaled.width() / dpr) / 2;
                    const int oy = (labelSize.height() - scaled.height() / dpr) / 2;
                    const QPoint inLabel = local - m_petLabel->pos();
                    const int pxX = static_cast<int>((inLabel.x() - ox) * dpr);
                    const int pxY = static_cast<int>((inLabel.y() - oy) * dpr);
                    if (pxX < 0 || pxY < 0 || pxX >= scaled.width() || pxY >= scaled.height()) {
                        *result = HTTRANSPARENT;
                        return true;
                    }
                    const QImage img = scaled.toImage();
                    if (qAlpha(img.pixel(pxX, pxY)) < 20) {
                        *result = HTTRANSPARENT;
                        return true;
                    }
                }
            }
            *result = HTCLIENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void PetWindow::closeEvent(QCloseEvent* event)
{
    // Closing the pet window hides it; use tray Exit to quit
    event->ignore();
    hidePet();
}
