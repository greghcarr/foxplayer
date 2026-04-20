#import <Cocoa/Cocoa.h>
#include "StatusBarItem.h"

using FoxPlayer::StatusBarItem;

typedef NS_ENUM(NSInteger, FoxBarState) {
    FoxBarStopped = 0,
    FoxBarPlaying,
    FoxBarPaused
};

// ---------------------------------------------------------------------------
// FoxStatusBarController
// ---------------------------------------------------------------------------
@interface FoxStatusBarController : NSObject
@property (nonatomic, strong) NSStatusItem* statusItem;
- (instancetype)initWithOwner:(StatusBarItem*)owner;
- (void)setState:(FoxBarState)state;
@end

@implementation FoxStatusBarController
{
    StatusBarItem* _owner;
}

- (instancetype)initWithOwner:(StatusBarItem*)owner
{
    if (!(self = [super init])) return nil;
    _owner = owner;
    [self setup];
    return self;
}

- (void)dealloc
{
    [NSStatusBar.systemStatusBar removeStatusItem:self.statusItem];
    self.statusItem = nil;
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)setup
{
    self.statusItem = [NSStatusBar.systemStatusBar
                       statusItemWithLength:NSSquareStatusItemLength];
    self.statusItem.button.image  = [self iconForState:FoxBarStopped];
    self.statusItem.button.target = self;
    self.statusItem.button.action = @selector(buttonClicked:);
    [self.statusItem.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    self.statusItem.visible = YES;
}

- (void)buttonClicked:(id)sender
{
    [self showContextMenu];
}

- (void)showContextMenu
{
    NSMenu* menu = [[NSMenu alloc] init];

    NSMenuItem* showItem = [[NSMenuItem alloc] initWithTitle:@"Show FoxPlayer"
                                                      action:@selector(menuShowApp:)
                                               keyEquivalent:@""];
    showItem.target = self;
    [menu addItem:showItem];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit FoxPlayer"
                                                      action:@selector(menuQuit:)
                                               keyEquivalent:@""];
    quitItem.target = self;
    [menu addItem:quitItem];

    if (NSStatusBarButton* btn = self.statusItem.button)
        [menu popUpMenuPositioningItem:nil
                            atLocation:NSMakePoint(0, btn.bounds.size.height)
                                inView:btn];
}

- (void)menuShowApp:(id)sender { if (_owner && _owner->onShowApp) _owner->onShowApp(); }
- (void)menuQuit:(id)sender    { if (_owner && _owner->onQuit)    _owner->onQuit(); }

- (void)setState:(FoxBarState)state
{
    self.statusItem.button.image = [self iconForState:state];
}

- (NSImage*)iconForState:(FoxBarState)state
{
    const CGFloat sz = 18.0;
    NSImage* img = [NSImage imageWithSize:NSMakeSize(sz, sz)
                                  flipped:NO
                           drawingHandler:^BOOL(NSRect) {
        [[NSColor blackColor] setFill];
        NSBezierPath* path = [NSBezierPath bezierPath];

        if (state == FoxBarPlaying)
        {
            CGFloat barW = sz * 0.22, barH = sz * 0.55, gap = sz * 0.14;
            CGFloat totalW = 2.0 * barW + gap;
            CGFloat sx = (sz - totalW) * 0.5, sy = (sz - barH) * 0.5;
            CGFloat r2 = barW * 0.25;
            [path appendBezierPathWithRoundedRect:NSMakeRect(sx, sy, barW, barH)
                                          xRadius:r2 yRadius:r2];
            [path appendBezierPathWithRoundedRect:NSMakeRect(sx + barW + gap, sy, barW, barH)
                                          xRadius:r2 yRadius:r2];
        }
        else if (state == FoxBarPaused)
        {
            CGFloat h = sz * 0.55, w = h * 0.866;
            CGFloat ox = (sz - w) * 0.5 + sz * 0.04, oy = (sz - h) * 0.5;
            [path moveToPoint:NSMakePoint(ox,     oy)];
            [path lineToPoint:NSMakePoint(ox + w, oy + h * 0.5)];
            [path lineToPoint:NSMakePoint(ox,     oy + h)];
            [path closePath];
        }
        else
        {
            CGFloat sq = sz * 0.46, ox = (sz - sq) * 0.5, oy = (sz - sq) * 0.5;
            [path appendBezierPathWithRoundedRect:NSMakeRect(ox, oy, sq, sq)
                                          xRadius:sq * 0.12 yRadius:sq * 0.12];
        }

        [path fill];
        return YES;
    }];
    [img setTemplate:YES];
    return img;
}

@end

// ---------------------------------------------------------------------------
// C++ StatusBarItem
// ---------------------------------------------------------------------------
namespace FoxPlayer
{

struct StatusBarItem::Impl
{
    CFTypeRef controller { nullptr };

    explicit Impl(StatusBarItem* owner)
    {
        FoxStatusBarController* ctrl =
            [[FoxStatusBarController alloc] initWithOwner:owner];
        controller = CFBridgingRetain(ctrl);
    }

    ~Impl()
    {
        if (controller) { CFBridgingRelease(controller); controller = nullptr; }
    }

    FoxStatusBarController* ctrl() const
    {
        return (__bridge FoxStatusBarController*)controller;
    }
};

StatusBarItem::StatusBarItem()
    : impl_(std::make_unique<Impl>(this)) {}

StatusBarItem::~StatusBarItem() = default;

void StatusBarItem::setState(State state)
{
    FoxBarState s = (state == State::Playing) ? FoxBarPlaying :
                    (state == State::Paused)  ? FoxBarPaused  :
                                                FoxBarStopped;
    [impl_->ctrl() setState:s];
}

} // namespace FoxPlayer
