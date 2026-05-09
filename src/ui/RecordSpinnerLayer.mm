#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "RecordSpinnerLayer.h"

// 33.3 RPM = 60 / 33.333 sec/rotation = 1.8 seconds per full turn.
static constexpr double kRotationSeconds = 1.8;

// ---------------------------------------------------------------------------
// Layer-backed NSView that hosts the spinning disc. The view itself fills the
// host bounds; a single sublayer (anchored at its center) carries the disc
// image and the rotation animation.
// ---------------------------------------------------------------------------
@interface StylusRecordSpinnerView : NSView
@property (nonatomic, strong) CALayer* discLayer;
// Frozen rotation angle (radians) used to bridge pause/resume cleanly. Stored
// as a scalar rather than reading and writing CATransform3D matrices across
// pause/resume cycles, which would compound floating-point error in the
// matrix elements until the disc visibly drifts toward an ellipse.
@property (nonatomic, assign) CGFloat currentAngle;
- (void)setDiscImage:(CGImageRef)cg;
- (void)setSpinning:(BOOL)spinning;
- (void)updateDimForKeyState;
@end

// Dim factor applied to the view when its window isn't key. Keeps the disc
// from looking jarringly bright next to the JUCE-rendered chrome that macOS
// has darkened for the inactive state. Tuned to roughly match the appearance
// of the rest of the bar on resign-key.
static const CGFloat kInactiveDimAlpha = 0.5;

@implementation StylusRecordSpinnerView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.wantsLayer = YES;
        if (!self.layer) self.layer = [CALayer layer];
        self.layer.masksToBounds = NO;

        _discLayer = [CALayer layer];
        // Use ResizeAspect, not Resize: the disc image is always square, but
        // the host view's bounds can end up slightly non-square due to
        // subpixel rounding between JUCE's int coordinates and AppKit's
        // float coordinates. Resize would stretch the square image to fill,
        // so even a tiny non-squareness in bounds renders the rotating disc
        // as a slowly-eccentric oval. ResizeAspect preserves the image's
        // own aspect ratio regardless of bounds.
        _discLayer.contentsGravity = kCAGravityResizeAspect;
        _discLayer.anchorPoint     = CGPointMake(0.5, 0.5);
        // Set bounds + position rather than frame: once the layer has a
        // rotation transform applied (during spin or after pause), the frame
        // property is undefined per Apple's docs and re-deriving bounds
        // through the inverse transform introduces scale drift each call.
        _discLayer.bounds          = CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height);
        _discLayer.position        = CGPointMake(NSMidX(self.bounds), NSMidY(self.bounds));
        [self.layer addSublayer:_discLayer];
        // Initial scale; refined in viewDidChangeBackingProperties once the
        // view is in a window so we know which display we're actually on.
        self.layer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        _discLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
    }
    return self;
}

// Re-subscribe to window-key notifications whenever the view moves to a new
// window, and apply the current dim state immediately. Without this the disc
// stays at full alpha when its window isn't key, which makes it stand out
// against JUCE's inactive-state rendering of the rest of the bar.
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc removeObserver:self name:NSWindowDidBecomeKeyNotification object:nil];
    [nc removeObserver:self name:NSWindowDidResignKeyNotification object:nil];
    if (self.window != nil)
    {
        [nc addObserver:self selector:@selector(updateDimForKeyState)
                   name:NSWindowDidBecomeKeyNotification object:self.window];
        [nc addObserver:self selector:@selector(updateDimForKeyState)
                   name:NSWindowDidResignKeyNotification object:self.window];
    }
    [self updateDimForKeyState];
}

- (void)updateDimForKeyState
{
    const BOOL isKey = (self.window != nil && self.window.isKeyWindow);
    self.alphaValue = isKey ? 1.0 : kInactiveDimAlpha;
}

// Fired when the view moves between displays with different backing scales
// (e.g. dragging the window from a Retina built-in display to a 1x external
// monitor). Updating contentsScale on the layer keeps the CGImage rendering
// at native resolution; without this, the disc looks oversampled or soft.
- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    const CGFloat scale = self.window != nil
                              ? self.window.backingScaleFactor
                              : NSScreen.mainScreen.backingScaleFactor;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.layer.contentsScale  = scale;
    _discLayer.contentsScale  = scale;
    [CATransaction commit];
}

- (BOOL)isFlipped { return YES; }

// The spinner is a purely visual overlay - clicks should fall through to the
// JUCE TransportBar underneath so it can resolve them as album-art /
// seek-bar / button hits. Returning nil from hitTest tells AppKit to keep
// looking for an event recipient.
- (NSView*)hitTest:(NSPoint)point { return nil; }

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)setFrame:(NSRect)frame
{
    [super setFrame:frame];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    // Avoid setting _discLayer.frame: with a rotation transform applied, the
    // frame setter back-computes bounds via the inverse transform and the
    // round-trip slowly skews the bounds, making the disc look oval after
    // many resize/layout passes.
    _discLayer.bounds   = CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height);
    _discLayer.position = CGPointMake(NSMidX(self.bounds), NSMidY(self.bounds));
    [CATransaction commit];
}

- (void)setDiscImage:(CGImageRef)cg
{
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _discLayer.contents = (__bridge id)cg;
    [CATransaction commit];
}

- (void)setSpinning:(BOOL)spinning
{
    if (spinning)
    {
        if ([_discLayer animationForKey:@"rotation"] != nil) return;

        // Animate from the frozen angle so resume picks up where pause left
        // off, instead of snapping back to angle 0 each time.
        CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
        a.fromValue           = @(_currentAngle);
        a.toValue             = @(_currentAngle + 2.0 * M_PI);
        a.duration            = kRotationSeconds;
        a.repeatCount         = HUGE_VALF;
        a.removedOnCompletion = NO;
        a.timingFunction      = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
        [_discLayer addAnimation:a forKey:@"rotation"];
    }
    else
    {
        // Freeze at the current visible angle. Extract the scalar rotation
        // from the presentation layer (atan2 over the rotation matrix's m11
        // and m12 columns), store it, and rebuild a pristine rotation matrix
        // for the model layer. Doing this scalar-first avoids the matrix-
        // drift mode where reading and writing presented.transform across
        // many cycles slowly accumulates non-rotation components, making the
        // disc look gradually oval.
        CALayer* presented = (CALayer*)[_discLayer presentationLayer];
        if (presented != nil)
        {
            const CATransform3D t = presented.transform;
            _currentAngle = atan2(t.m12, t.m11);
        }
        [_discLayer removeAnimationForKey:@"rotation"];
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        _discLayer.transform = CATransform3DMakeRotation(_currentAngle, 0, 0, 1);
        [CATransaction commit];
    }
}

@end

// ---------------------------------------------------------------------------
// C++ wrapper
// ---------------------------------------------------------------------------
namespace Stylus
{

namespace
{
// Convert a juce::Image to a CGImageRef. Caller owns and must CGImageRelease.
// Returns nullptr on invalid input.
CGImageRef makeCGImageFromJuceImage(const juce::Image& image)
{
    if (! image.isValid()) return nullptr;

    juce::Image rgba = image.convertedToFormat(juce::Image::ARGB);
    juce::Image::BitmapData bm(rgba, juce::Image::BitmapData::readOnly);

    const size_t bytesPerRow = (size_t) bm.lineStride;
    const size_t totalBytes  = bytesPerRow * (size_t) bm.height;

    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                  (const UInt8*) bm.data,
                                  (CFIndex) totalBytes);
    if (data == nullptr) return nullptr;

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (provider == nullptr) return nullptr;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef cg = CGImageCreate((size_t) bm.width,
                                  (size_t) bm.height,
                                  8,
                                  32,
                                  bytesPerRow,
                                  cs,
                                  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
                                  provider,
                                  nullptr,
                                  false,
                                  kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    return cg;
}

} // namespace

RecordSpinnerLayer::RecordSpinnerLayer()
{
    StylusRecordSpinnerView* view =
        [[StylusRecordSpinnerView alloc] initWithFrame:NSZeroRect];
    discView_ = (void*) CFBridgingRetain(view);
    setView(discView_);  // juce::NSViewComponent takes its own retain

    // Pass clicks through to the JUCE TransportBar underneath so it can route
    // album-art / seek-bar / button hits. The NSView returns nil from
    // hitTest:, but the JUCE Component side of NSViewComponent still claims
    // mouse clicks by default and would swallow them otherwise.
    setInterceptsMouseClicks(false, false);
}

RecordSpinnerLayer::~RecordSpinnerLayer()
{
    setView(nullptr);
    if (discView_ != nullptr)
    {
        CFBridgingRelease(discView_);
        discView_ = nullptr;
    }
}

void RecordSpinnerLayer::setImage(const juce::Image& image)
{
    if (discView_ == nullptr) return;
    auto* view = (__bridge StylusRecordSpinnerView*) discView_;
    CGImageRef cg = makeCGImageFromJuceImage(image);
    [view setDiscImage:cg];
    if (cg != nullptr) CGImageRelease(cg);
}

void RecordSpinnerLayer::setSpinning(bool shouldSpin)
{
    if (discView_ == nullptr) return;
    auto* view = (__bridge StylusRecordSpinnerView*) discView_;
    [view setSpinning:(shouldSpin ? YES : NO)];
}

} // namespace Stylus
