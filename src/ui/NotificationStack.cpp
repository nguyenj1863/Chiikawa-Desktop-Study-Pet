#include "ui/NotificationStack.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWidget>
#include <algorithm>

NotificationStack& NotificationStack::shared()
{
    static NotificationStack* instance = [] {
        auto* stack = new NotificationStack(QCoreApplication::instance());
        return stack;
    }();
    return *instance;
}

NotificationStack::NotificationStack(QObject* parent)
    : QObject(parent)
{
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) { relayout(); });
        connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen* removed) {
            if (removed == m_screen) {
                m_screen = nullptr;
            }
            relayout();
        });
    }
    bindScreen(targetScreen());
}

QPoint NotificationStack::bottomRightPosition(const QRect& available, const QSize& size, int margin)
{
    const int safeMargin = std::max(0, margin);
    const int rawX = available.right() - size.width() - safeMargin + 1;
    const int rawY = available.bottom() - size.height() - safeMargin + 1;
    const int maxX = std::max(available.left(), available.right() - size.width() + 1);
    const int maxY = std::max(available.top(), available.bottom() - size.height() + 1);
    return {std::clamp(rawX, available.left(), maxX), std::clamp(rawY, available.top(), maxY)};
}

QList<QPoint> NotificationStack::stackPositions(const QRect& available, const QList<QSize>& sizes, int margin, int gap)
{
    QList<QPoint> out;
    int bottom = available.bottom() - std::max(0, margin) + 1;   // one past the lowest pixel the next item may use
    for (const QSize& size : sizes) {
        const QPoint corner = bottomRightPosition(available, size, margin);
        const int y = std::max(available.top(), bottom - size.height());
        out.append({corner.x(), y});
        bottom = y - std::max(0, gap);
    }
    return out;
}

QScreen* NotificationStack::targetScreen() const
{
    if (QScreen* primary = QGuiApplication::primaryScreen()) {
        return primary;
    }
    const auto all = QGuiApplication::screens();
    return all.isEmpty() ? nullptr : all.first();
}

void NotificationStack::bindScreen(QScreen* screen)
{
    if (m_screen == screen) {
        return;
    }
    if (m_screen) {
        disconnect(m_screen, nullptr, this, nullptr);
    }
    m_screen = screen;
    if (m_screen) {
        connect(m_screen, &QScreen::availableGeometryChanged, this, [this](const QRect&) { relayout(); });
        connect(m_screen, &QScreen::geometryChanged, this, [this](const QRect&) { relayout(); });
    }
}

QRect NotificationStack::workArea() const
{
    if (m_override.isValid()) {
        return m_override;
    }
    QScreen* screen = targetScreen();
    return screen ? screen->availableGeometry() : QRect();
}

void NotificationStack::prune()
{
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](const QPointer<QWidget>& w) { return w.isNull(); }),
                  m_items.end());
}

QList<QWidget*> NotificationStack::notifications() const
{
    QList<QWidget*> out;
    for (const QPointer<QWidget>& w : m_items) {
        if (w) {
            out.append(w.data());
        }
    }
    return out;
}

namespace {

/** Moves a notification, first stopping its own slide-in so the animation cannot move it back. */
void moveTo(QWidget* w, const QPoint& pos)
{
    for (QPropertyAnimation* anim : w->findChildren<QPropertyAnimation*>(Qt::FindDirectChildrenOnly)) {
        if (anim->targetObject() == w && anim->propertyName() == "pos" && anim->state() == QAbstractAnimation::Running) {
            anim->stop();
        }
    }
    w->move(pos);
}

} // namespace

QPoint NotificationStack::place(QWidget* notification)
{
    if (!notification) {
        return {};
    }
    prune();
    m_items.removeAll(QPointer<QWidget>(notification));
    m_items.prepend(notification);
    notification->removeEventFilter(this);
    notification->installEventFilter(this);
    notification->adjustSize();
    bindScreen(targetScreen());

    const QRect area = workArea();
    QList<QSize> sizes;
    for (const QPointer<QWidget>& w : std::as_const(m_items)) {
        sizes.append(w->size());
    }
    const QList<QPoint> positions = stackPositions(area, sizes);
    for (qsizetype i = 1; i < m_items.size(); ++i) {
        if (m_items.at(i)->isVisible()) {
            moveTo(m_items.at(i), positions.at(i));
        }
    }
    return positions.isEmpty() ? QPoint() : positions.first();
}

void NotificationStack::remove(QWidget* notification)
{
    if (!notification) {
        return;
    }
    notification->removeEventFilter(this);
    const qsizetype before = m_items.size();
    m_items.removeAll(QPointer<QWidget>(notification));
    if (m_items.size() != before) {
        relayout();
    }
}

void NotificationStack::relayout()
{
    prune();
    bindScreen(targetScreen());
    const QRect area = workArea();
    if (!area.isValid()) {
        return;
    }
    QList<QSize> sizes;
    for (const QPointer<QWidget>& w : std::as_const(m_items)) {
        sizes.append(w->size());
    }
    const QList<QPoint> positions = stackPositions(area, sizes);
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->isVisible()) {
            moveTo(m_items.at(i), positions.at(i));
        }
    }
}

bool NotificationStack::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Hide) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            remove(w);
        }
    }
    return QObject::eventFilter(watched, event);
}
