#include <QGuiApplication>

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>

namespace SpotifyLauncherMac {

namespace {
NSString* const kSpotifyBundleId = @"com.spotify.client";
} // namespace

bool activateRunning()
{
    @autoreleasepool {
        NSArray<NSRunningApplication*>* apps =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:kSpotifyBundleId];
        if ([apps count] == 0) {
            return false;
        }
        NSRunningApplication* spotify = [apps firstObject];
        if ([spotify isHidden]) {
            [spotify unhide];
        }
        const BOOL activated = [spotify activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        // Also ask NSWorkspace to reopen the main window if it was closed while running.
        NSURL* appUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:kSpotifyBundleId];
        if (appUrl != nil) {
            NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
            cfg.activates = YES;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl
                                                  configuration:cfg
                                              completionHandler:nil];
        }
        return activated == YES || appUrl != nil;
    }
}

bool isInstalled()
{
    @autoreleasepool {
        NSURL* appUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:kSpotifyBundleId];
        return appUrl != nil;
    }
}

bool launchInstalled()
{
    @autoreleasepool {
        NSURL* appUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:kSpotifyBundleId];
        if (appUrl == nil) {
            return false;
        }
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        cfg.activates = YES;
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl
                                              configuration:cfg
                                          completionHandler:nil];
        return true;
    }
}

} // namespace SpotifyLauncherMac
#endif
