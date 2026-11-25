#include "MacWindowHelper.h"

#include <QGuiApplication>
#include <QWidget>

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

} // namespace MacWindowHelper
