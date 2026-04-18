#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "MacWindowHelper.h"

void FoxPlayer_activateAndShowWindow(void* nativeHandle)
{
    NSView*   view   = (__bridge NSView*)nativeHandle;
    NSWindow* window = view.window;
    if (window == nil) return;

    NSWindowCollectionBehavior old = window.collectionBehavior;
    window.collectionBehavior = old | NSWindowCollectionBehaviorMoveToActiveSpace;

    if (@available(macOS 14.0, *))
        [NSApp activate];
    else
        [NSApp activateIgnoringOtherApps:YES];

    [window orderFrontRegardless];
    [window makeKeyWindow];

    window.collectionBehavior = old;
}

// ---------------------------------------------------------------------------
// Dock icon / app-reopen support.
//
// Two mechanisms are combined:
//   1. ObjC method replacement on JUCE's app delegate to intercept
//      applicationShouldHandleReopen:hasVisibleWindows:. Covers the Dock click
//      while FoxPlayer is already the active app. Also prevents JUCE's default
//      behaviour of calling systemRequestedQuit() (which would quit the app).
//   2. NSApplicationDidBecomeActiveNotification fallback for the case where the
//      user switched to another app and then returned via Cmd+Tab or Dock.
// ---------------------------------------------------------------------------

// Owns the callback block with a `copy` property so non-ARC block lifetime is
// handled correctly — a plain `static void (^)() = ^{...}` stores a stack
// block that becomes a dangling pointer once the enclosing function returns.
@interface FoxPlayerReopenHandler : NSObject
@property (nonatomic, copy) void (^onReopen)(void);
- (void)fireIfNoVisibleWindows;
- (void)appDidBecomeActive:(NSNotification*)note;
@end

@implementation FoxPlayerReopenHandler
- (void)fireIfNoVisibleWindows
{
    Class sbClass = NSClassFromString(@"NSStatusBarWindow");
    for (NSWindow* w in NSApp.windows)
    {
        if (sbClass && [w isKindOfClass:sbClass]) continue;
        if (w.visible && !w.miniaturized) return;
    }
    if (_onReopen) _onReopen();
}
- (void)appDidBecomeActive:(NSNotification*)note
{
    [self fireIfNoVisibleWindows];
}
@end

static FoxPlayerReopenHandler* gHandler = nil;
static BOOL gSwizzleApplied = NO;

// C function installed as the implementation of
// applicationShouldHandleReopen:hasVisibleWindows: on JUCE's delegate class.
static BOOL FoxPlayer_applicationShouldHandleReopen(id, SEL, NSApplication*, BOOL)
{
    // Dock click is an explicit user action — always show the window.
    if (gHandler && gHandler.onReopen)
        gHandler.onReopen();
    return YES;
}

void FoxPlayer_setDockReopenCallback(std::function<void()> callback)
{
    // Tear down any previous handler.
    if (gHandler)
    {
        [NSNotificationCenter.defaultCenter removeObserver:gHandler];
#if !__has_feature(objc_arc)
        [gHandler release];
#endif
        gHandler = nil;
    }

    if (!callback) return;

    gHandler = [[FoxPlayerReopenHandler alloc] init];
    // Copy the std::function into the block so the block owns it independently.
    gHandler.onReopen = ^{ callback(); };

    // --- Mechanism 1: replace applicationShouldHandleReopen on JUCE's delegate ---
    if (!gSwizzleApplied && NSApp.delegate != nil)
    {
        gSwizzleApplied = YES;
        Class cls = [NSApp.delegate class];
        SEL   sel = @selector(applicationShouldHandleReopen:hasVisibleWindows:);
        IMP   imp = (IMP)FoxPlayer_applicationShouldHandleReopen;
        // "c@:@c" — BOOL return, id self, SEL _cmd, NSApplication*, BOOL
        if (class_respondsToSelector(cls, sel)) {
            Method m = class_getInstanceMethod(cls, sel);
            if (m != nil)
                method_setImplementation(m, imp);
        } else
            class_addMethod(cls, sel, imp, "c@:@c");
    }

    // --- Mechanism 2: notification fallback ---
    [NSNotificationCenter.defaultCenter
        addObserver:gHandler
           selector:@selector(appDidBecomeActive:)
               name:NSApplicationDidBecomeActiveNotification
             object:nil];
}
