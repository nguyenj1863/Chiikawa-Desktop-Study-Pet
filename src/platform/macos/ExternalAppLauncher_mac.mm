#include "quickactions/ExternalAppLauncher.h"

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#include <QImage>
#include <QPixmap>

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

bool activateRunningCustom(const QString& appPath)
{
    @autoreleasepool {
        NSString* nsPath = appPath.toNSString();
        NSBundle* bundle = [NSBundle bundleWithPath:nsPath];
        NSString* bundleId = [bundle bundleIdentifier];
        if (bundleId == nil) {
            return false;
        }
        NSArray<NSRunningApplication*>* apps =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId];
        if ([apps count] == 0) {
            return false;
        }
        NSRunningApplication* running = [apps firstObject];
        if ([running isHidden]) {
            [running unhide];
        }
        return [running activateWithOptions:NSApplicationActivateIgnoringOtherApps] == YES;
    }
}

bool isInstalledCustom(const QString& appPath)
{
    @autoreleasepool {
        NSString* nsPath = appPath.toNSString();
        return [[NSFileManager defaultManager] fileExistsAtPath:nsPath] == YES;
    }
}

bool launchInstalledCustom(const QString& appPath)
{
    @autoreleasepool {
        NSString* nsPath = appPath.toNSString();
        if (![[NSFileManager defaultManager] fileExistsAtPath:nsPath]) {
            return false;
        }
        NSURL* appUrl = [NSURL fileURLWithPath:nsPath];
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        cfg.activates = YES;
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl
                                              configuration:cfg
                                          completionHandler:nil];
        return true;
    }
}

QIcon extractAppIcon(const QString& appPath)
{
    @autoreleasepool {
        NSString* nsPath = appPath.toNSString();
        if (![[NSFileManager defaultManager] fileExistsAtPath:nsPath]) {
            return {};
        }
        NSImage* nsImage = [[NSWorkspace sharedWorkspace] iconForFile:nsPath];
        if (nsImage == nil) {
            return {};
        }
        NSRect rect = NSMakeRect(0, 0, 128, 128);
        CGImageRef cgRef = [nsImage CGImageForProposedRect:&rect context:nil hints:nil];
        if (cgRef == nullptr) {
            return {};
        }
        const size_t width = CGImageGetWidth(cgRef);
        const size_t height = CGImageGetHeight(cgRef);
        if (width == 0 || height == 0) {
            return {};
        }
        QImage qImg(static_cast<int>(width), static_cast<int>(height), QImage::Format_ARGB32_Premultiplied);
        qImg.fill(Qt::transparent);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(qImg.bits(), width, height, 8,
                                                 static_cast<size_t>(qImg.bytesPerLine()), colorSpace,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
        CGColorSpaceRelease(colorSpace);
        if (ctx == nullptr) {
            return {};
        }
        CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), cgRef);
        CGContextRelease(ctx);
        return QIcon(QPixmap::fromImage(qImg));
    }
}

} // namespace ExternalAppLauncherMac
#endif
