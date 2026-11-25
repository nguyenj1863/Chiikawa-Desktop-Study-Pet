#include "DesktopLayerHelper.h"

#include <QWidget>

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include "MacWindowHelper.h"
#endif

DesktopLayerHelper::DesktopLayerHelper()
{
#ifdef Q_OS_WIN
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
#endif
}

void DesktopLayerHelper::applyMode(QWidget* petWindow, bool desktopOnly)
{
    if (!isSupported()) {
        desktopOnly = false;
    }
    m_desktopOnly = desktopOnly;
    if (!petWindow) {
        return;
    }

    const bool wasVisible = petWindow->isVisible();
    const QPoint savedPos = petWindow->pos();

#ifdef Q_OS_MACOS
    // On macOS, Qt::Tool creates an NSPanel that hides when another app activates.
    // LSUIElement=true in Info.plist already hides the Dock icon, so Qt::Window keeps Chiikawa visible.
    // Qt::NoDropShadowWindowHint prevents AppKit from caching a stale shadow of the initial sprite frame.
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Window | Qt::NoDropShadowWindowHint;
#else
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint;
#endif
    if (desktopOnly) {
        flags |= Qt::WindowStaysOnBottomHint;
    } else {
        flags |= Qt::WindowStaysOnTopHint;
    }

    if (petWindow->windowFlags() != flags) {
        petWindow->setWindowFlags(flags);
        petWindow->setAttribute(Qt::WA_TranslucentBackground);
        petWindow->setAttribute(Qt::WA_ShowWithoutActivating);
        petWindow->move(savedPos);
        if (wasVisible) {
            petWindow->show();
        }
    }

#ifdef Q_OS_WIN
    auto* hwnd = reinterpret_cast<HWND>(petWindow->winId());
    if (!hwnd) {
        return;
    }

    // Ensure WS_EX_NOACTIVATE so clicking the pet never activates it above user apps in Desktop-Only Mode
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (desktopOnly) {
        exStyle |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    } else {
        exStyle &= ~WS_EX_NOACTIVATE;
        exStyle |= WS_EX_TOOLWINDOW;
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    if (desktopOnly) {
        enforceBottomZOrder(petWindow);
    } else {
        SetWindowPos(hwnd,
                     HWND_TOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#elif defined(Q_OS_MACOS)
    MacWindowHelper::configureFloatingOverlay(petWindow, true);
#endif
}

void DesktopLayerHelper::enforceBottomZOrder(QWidget* petWindow)
{
#ifdef Q_OS_WIN
    if (!petWindow) {
        return;
    }
    auto* hwnd = reinterpret_cast<HWND>(petWindow->winId());
    if (!hwnd) {
        return;
    }
    // Defensive Progman presence check; keep top-level HWND pinned at HWND_BOTTOM
    // so clicks, dragging, and transparency stay intact while sitting behind all apps.
    (void)FindWindowW(L"Progman", nullptr);
    SetWindowPos(hwnd,
                 HWND_BOTTOM,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(petWindow);
#endif
}

bool DesktopLayerHelper::handleNativeEvent(QWidget* petWindow,
                                           const QByteArray& eventType,
                                           void* message,
                                           qintptr* result)
{
#ifdef Q_OS_WIN
    if (!petWindow || !message) {
        return false;
    }
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);

    // Re-apply desktop layering if Windows Explorer restarts
    if (m_taskbarCreatedMsg != 0 && msg->message == m_taskbarCreatedMsg) {
        if (m_desktopOnly) {
            enforceBottomZOrder(petWindow);
        }
        return false;
    }

    // Prevent Windows from raising the pet above normal applications in Desktop-Only Mode
    if (m_desktopOnly && msg->message == WM_WINDOWPOSCHANGING) {
        auto* wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
        if (wp) {
            wp->hwndInsertAfter = HWND_BOTTOM;
            wp->flags &= ~SWP_NOZORDER;
            wp->flags |= SWP_NOACTIVATE;
        }
        return false;
    }

    Q_UNUSED(result);
#else
    Q_UNUSED(petWindow);
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
