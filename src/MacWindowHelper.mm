#include "MacWindowHelper.h"

#include <QGuiApplication>
#include <QWidget>
#include <memory>

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#endif

namespace MacWindowHelper {

void configureFloatingOverlay(QWidget* widget, bool disableShadow)
{
#ifdef Q_OS_MACOS
    if (!widget) {
        return;
    }
    // Never reinterpret winId() as an NSView* when running under offscreen/headless QPA plugins.
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }

    auto* view = reinterpret_cast<NSView*>(widget->winId());
    if (!view) {
        return;
    }
    NSWindow* window = [view window];
    if (!window) {
        return;
    }

    [window setLevel:NSFloatingWindowLevel];
    [window setHidesOnDeactivate:NO];

    NSWindowCollectionBehavior behavior = [window collectionBehavior];
    // CanJoinAllSpaces is mutually exclusive with MoveToActiveSpace.
    behavior &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
    behavior &= ~NSWindowCollectionBehaviorManaged;
    behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces
              | NSWindowCollectionBehaviorFullScreenAuxiliary
              | NSWindowCollectionBehaviorIgnoresCycle;
    [window setCollectionBehavior:behavior];

    if (disableShadow) {
        [window setHasShadow:NO];
    }
#else
    Q_UNUSED(widget);
    Q_UNUSED(disableShadow);
#endif
}

bool prefersReducedMotion()
{
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return false;
    }
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] == YES;
#else
    return false;
#endif
}

void* installOutsideClickMonitor(std::function<void()> callback)
{
#ifdef Q_OS_MACOS
    if (!callback || QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return nullptr;
    }
    auto sharedCb = std::make_shared<std::function<void()>>(std::move(callback));
    id monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:
                     (NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown)
                                                        handler:^(NSEvent* /*event*/) {
        if (*sharedCb) {
            (*sharedCb)();
        }
    }];
    return (__bridge_retained void*)monitor;
#else
    Q_UNUSED(callback);
    return nullptr;
#endif
}

void removeOutsideClickMonitor(void* token)
{
#ifdef Q_OS_MACOS
    if (!token) {
        return;
    }
    id monitor = (__bridge_transfer id)token;
    [NSEvent removeMonitor:monitor];
#else
    Q_UNUSED(token);
#endif
}

} // namespace MacWindowHelper
