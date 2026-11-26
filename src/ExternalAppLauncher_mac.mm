#include "ExternalAppLauncher.h"

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>

namespace ExternalAppLauncherMac {

namespace {

NSArray<NSString*>* bundleIdsFor(ExternalAppLauncher::AppId app)
{
    switch (app) {
    case ExternalAppLauncher::AppId::Spotify:
        return @[@"com.spotify.client"];
    case ExternalAppLauncher::AppId::Discord:
        return @[@"com.hnc.Discord"];
    case ExternalAppLauncher::AppId::LeagueOfLegends:
        return @[
            @"com.riotgames.LeagueofLegends.LeagueClientUx",
            @"com.riotgames.LeagueofLegends.GameClient",
            @"com.riotgames.RiotGames.RiotClient"
        ];
    case ExternalAppLauncher::AppId::Cursor:
        return @[@"com.todesktop.230313mzl4w4u92"];
    case ExternalAppLauncher::AppId::Count:
        break;
    }
    return @[];
}

NSArray<NSString*>* fallbackAppPathsFor(ExternalAppLauncher::AppId app)
{
    switch (app) {
    case ExternalAppLauncher::AppId::Spotify:
        return @[@"/Applications/Spotify.app"];
    case ExternalAppLauncher::AppId::Discord:
        return @[@"/Applications/Discord.app"];
    case ExternalAppLauncher::AppId::LeagueOfLegends:
        return @[@"/Applications/League of Legends.app", @"/Applications/Riot Client.app"];
    case ExternalAppLauncher::AppId::Cursor:
        return @[@"/Applications/Cursor.app"];
    case ExternalAppLauncher::AppId::Count:
        break;
    }
    return @[];
}

NSURL* resolveAppUrl(ExternalAppLauncher::AppId app)
{
    NSWorkspace* ws = [NSWorkspace sharedWorkspace];
    for (NSString* bundleId in bundleIdsFor(app)) {
        NSURL* url = [ws URLForApplicationWithBundleIdentifier:bundleId];
        if (url != nil) {
            return url;
        }
    }
    NSFileManager* fm = [NSFileManager defaultManager];
    for (NSString* path in fallbackAppPathsFor(app)) {
        if ([fm fileExistsAtPath:path]) {
            return [NSURL fileURLWithPath:path];
        }
    }
    return nil;
}

} // namespace

bool activateRunning(ExternalAppLauncher::AppId app)
{
    @autoreleasepool {
        for (NSString* bundleId in bundleIdsFor(app)) {
            NSArray<NSRunningApplication*>* apps =
                [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId];
            if ([apps count] == 0) {
                continue;
            }
            NSRunningApplication* running = [apps firstObject];
            if ([running isHidden]) {
                [running unhide];
            }
            const BOOL activated = [running activateWithOptions:NSApplicationActivateIgnoringOtherApps];
            NSURL* appUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:bundleId];
            if (appUrl != nil) {
                NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
                cfg.activates = YES;
                [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl
                                                      configuration:cfg
                                                  completionHandler:nil];
            }
            return activated == YES || appUrl != nil;
        }
        return false;
    }
}

bool isInstalled(ExternalAppLauncher::AppId app)
{
    @autoreleasepool {
        return resolveAppUrl(app) != nil;
    }
}

bool launchInstalled(ExternalAppLauncher::AppId app)
{
    @autoreleasepool {
        NSURL* appUrl = resolveAppUrl(app);
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

} // namespace ExternalAppLauncherMac
#endif
