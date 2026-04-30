#import <Cocoa/Cocoa.h>
#import <MediaPlayer/MediaPlayer.h>
#import <IOKit/hidsystem/ev_keymap.h>
#import <ApplicationServices/ApplicationServices.h>
#include "NowPlayingBridge.h"

using FoxPlayer::NowPlayingBridge;

// ---------------------------------------------------------------------------
// ObjC controller
// ---------------------------------------------------------------------------
@interface FoxNowPlayingController : NSObject
@property (nonatomic, strong) id toggleTarget;
@property (nonatomic, strong) id nextTarget;
@property (nonatomic, strong) id prevTarget;
@property (nonatomic, strong) id localMonitor;
- (instancetype)initWithOwner:(NowPlayingBridge*)owner;
- (void)setTrackTitle:(NSString*)title artist:(NSString*)artist duration:(double)dur;
- (void)setPlaybackState:(BOOL)playing position:(double)pos;
- (void)clearNowPlaying;
- (BOOL)handleMediaKeyEvent:(NSEvent*)ev;
@end

// ---------------------------------------------------------------------------
// CGEventTap callback, fires at the CoreGraphics level, before any NSApp
// or MPRemoteCommandCenter handler sees the event. Returning NULL consumes it.
// ---------------------------------------------------------------------------
static CGEventRef foxMediaKeyTapCallback(CGEventTapProxy,
                                          CGEventType    type,
                                          CGEventRef     event,
                                          void*          refcon)
{
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
        return event;

    NSEvent* ev = [NSEvent eventWithCGEvent:event];
    FoxNowPlayingController* ctrl = (__bridge FoxNowPlayingController*)refcon;
    if ([ctrl handleMediaKeyEvent:ev]) return NULL; // consumed
    return event;
}

@implementation FoxNowPlayingController
{
    NowPlayingBridge*  _owner;
    double             _duration;
    BOOL               _playing;
    BOOL               _hasTrack;
    CFTimeInterval     _lastPlayPauseTime;
    CFMachPortRef      _eventTap;
    CFRunLoopSourceRef _tapSource;
}

- (instancetype)initWithOwner:(NowPlayingBridge*)owner
{
    if (!(self = [super init])) return nil;
    _owner             = owner;
    _lastPlayPauseTime = 0.0;
    [self registerCommands];
    return self;
}

- (void)dealloc
{
    [self unregisterCommands];
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

// ---------------------------------------------------------------------------
// Debounced play/pause: gate drops duplicate fires within 200 ms.
// Both the CGEventTap and MPRemoteCommandCenter can theoretically fire for
// the same key; the timestamp prevents a double-toggle.
// ---------------------------------------------------------------------------

- (void)firePlayPause
{
    CFTimeInterval now = CACurrentMediaTime();
    if (now - _lastPlayPauseTime < 0.2) return;
    _lastPlayPauseTime = now;
    if (_owner && _owner->onPlayPause) _owner->onPlayPause();
}

// ---------------------------------------------------------------------------
// Media key event parsing, shared by CGEventTap and local NSEvent monitor
// ---------------------------------------------------------------------------

- (BOOL)handleMediaKeyEvent:(NSEvent*)ev
{
    if (ev.type != NSEventTypeSystemDefined || ev.subtype != 8) return NO;

    int data1    = (int)ev.data1;
    int keyCode  = (int)(((unsigned int)data1 & 0xFFFF0000U) >> 16);
    int keyFlags = data1 & 0x0000FFFF;
    BOOL keyDown   = ((keyFlags & 0xFF00) == 0xA00);
    BOOL keyRepeat = ((keyFlags & 0x1) == 1);

    if (!keyDown || keyRepeat) return NO;

    if (keyCode == NX_KEYTYPE_PLAY) {
        [self firePlayPause];
        return YES;
    }
    if (keyCode == NX_KEYTYPE_NEXT || keyCode == NX_KEYTYPE_FAST) {
        if (_owner && _owner->onNext) _owner->onNext();
        return YES;
    }
    if (keyCode == NX_KEYTYPE_PREVIOUS || keyCode == NX_KEYTYPE_REWIND) {
        if (_owner && _owner->onPrevious) _owner->onPrevious();
        return YES;
    }
    return NO;
}

// ---------------------------------------------------------------------------
// CGEventTap, intercepts media keys globally before any other app sees them.
// Requires Accessibility permission; silently skips setup if not granted.
// macOS will show the Accessibility prompt the first time we call
// AXIsProcessTrustedWithOptions, pointing the user to System Settings.
// ---------------------------------------------------------------------------

- (void)setupEventTap
{
    // Trigger the system accessibility prompt (no-op if already trusted).
    NSDictionary* opts = @{ (__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES };
    if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts)) return;

    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        CGEventMaskBit(NSEventTypeSystemDefined),
        foxMediaKeyTapCallback,
        (__bridge void*)self);

    if (!tap) return; // Permission granted but tap still failed, fall back gracefully.

    _eventTap  = tap;
    _tapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), _tapSource, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
}

- (void)teardownEventTap
{
    if (_tapSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), _tapSource, kCFRunLoopCommonModes);
        CFRelease(_tapSource);
        _tapSource = NULL;
    }
    if (_eventTap) {
        CGEventTapEnable(_eventTap, false);
        CFRelease(_eventTap);
        _eventTap = NULL;
    }
}

// ---------------------------------------------------------------------------
// MPRemoteCommandCenter + local NSEvent monitor
// ---------------------------------------------------------------------------

- (void)registerCommands
{
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];

    center.stopCommand.enabled               = NO;
    center.playCommand.enabled               = NO;
    center.pauseCommand.enabled              = NO;
    center.changePlaybackRateCommand.enabled = NO;
    center.seekForwardCommand.enabled        = NO;
    center.seekBackwardCommand.enabled       = NO;

    center.togglePlayPauseCommand.enabled = YES;
    center.nextTrackCommand.enabled       = YES;
    center.previousTrackCommand.enabled   = YES;

    self.toggleTarget = [center.togglePlayPauseCommand
        addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
            [self firePlayPause];
            return MPRemoteCommandHandlerStatusSuccess;
        }];
    self.nextTarget = [center.nextTrackCommand
        addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
            if (self->_owner && self->_owner->onNext) self->_owner->onNext();
            return MPRemoteCommandHandlerStatusSuccess;
        }];
    self.prevTarget = [center.previousTrackCommand
        addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
            if (self->_owner && self->_owner->onPrevious) self->_owner->onPrevious();
            return MPRemoteCommandHandlerStatusSuccess;
        }];

    // Local monitor: backup for when CGEventTap isn't active. Consuming the
    // event (returning nil) prevents MPRemoteCommandCenter from also firing.
    __unsafe_unretained FoxNowPlayingController* bself = self;
    self.localMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskSystemDefined
        handler:^NSEvent*(NSEvent* ev) {
            if ([bself handleMediaKeyEvent:ev]) return nil;
            return ev;
        }];

    // Try to install the CGEventTap. Shows the Accessibility prompt if needed.
    [self setupEventTap];
}

- (void)unregisterCommands
{
    [self teardownEventTap];

    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    if (self.toggleTarget) [center.togglePlayPauseCommand removeTarget:self.toggleTarget];
    if (self.nextTarget)   [center.nextTrackCommand       removeTarget:self.nextTarget];
    if (self.prevTarget)   [center.previousTrackCommand   removeTarget:self.prevTarget];
    self.toggleTarget = nil;
    self.nextTarget   = nil;
    self.prevTarget   = nil;

    if (self.localMonitor) { [NSEvent removeMonitor:self.localMonitor]; self.localMonitor = nil; }
}

// ---------------------------------------------------------------------------
// MPNowPlayingInfoCenter updates
// ---------------------------------------------------------------------------

- (void)setTrackTitle:(NSString*)title artist:(NSString*)artist duration:(double)dur
{
    _duration = dur;
    _hasTrack = YES;
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    if (title.length  > 0) info[MPMediaItemPropertyTitle]           = title;
    if (artist.length > 0) info[MPMediaItemPropertyArtist]          = artist;
    if (dur > 0)           info[MPMediaItemPropertyPlaybackDuration] = @(dur);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(0.0);
    info[MPNowPlayingInfoPropertyPlaybackRate]        = @(_playing ? 1.0 : 0.0);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
    if (@available(macOS 10.12.2, *))
        [MPNowPlayingInfoCenter defaultCenter].playbackState =
            _playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

- (void)setPlaybackState:(BOOL)playing position:(double)pos
{
    if (!_hasTrack) return;

    _playing = playing;

    NSMutableDictionary* existing =
        [[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo mutableCopy];
    NSMutableDictionary* info = existing ? existing : [NSMutableDictionary dictionary];

    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(pos);
    info[MPNowPlayingInfoPropertyPlaybackRate]        = @(playing ? 1.0 : 0.0);
    if (_duration > 0)
        info[MPMediaItemPropertyPlaybackDuration] = @(_duration);

    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
    if (@available(macOS 10.12.2, *))
        [MPNowPlayingInfoCenter defaultCenter].playbackState =
            playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

- (void)clearNowPlaying
{
    _playing  = NO;
    _duration = 0.0;
    _hasTrack = NO;
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    if (@available(macOS 10.12.2, *))
        [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
}

@end

// ---------------------------------------------------------------------------
// C++ NowPlayingBridge
// ---------------------------------------------------------------------------
namespace FoxPlayer
{

NowPlayingBridge::NowPlayingBridge()
{
    FoxNowPlayingController* ctrl =
        [[FoxNowPlayingController alloc] initWithOwner:this];
    impl_ = (void*)CFBridgingRetain(ctrl);
}

NowPlayingBridge::~NowPlayingBridge()
{
    if (impl_) { CFBridgingRelease(impl_); impl_ = nullptr; }
}

void NowPlayingBridge::setTrackInfo(const std::string& title,
                                    const std::string& artist,
                                    double             dur)
{
    auto* c = (__bridge FoxNowPlayingController*)impl_;
    [c setTrackTitle:@(title.c_str()) artist:@(artist.c_str()) duration:dur];
}

void NowPlayingBridge::setPlaybackState(bool isPlaying, double pos)
{
    auto* c = (__bridge FoxNowPlayingController*)impl_;
    [c setPlaybackState:isPlaying ? YES : NO position:pos];
}

void NowPlayingBridge::clearNowPlaying()
{
    auto* c = (__bridge FoxNowPlayingController*)impl_;
    [c clearNowPlaying];
}

} // namespace FoxPlayer
