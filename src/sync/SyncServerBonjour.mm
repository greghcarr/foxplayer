#include "SyncServerBonjour.h"

#import <Foundation/Foundation.h>

namespace Stylus
{

namespace
{
    // Trivial delegate -- we don't need to react to NSNetService
    // lifecycle events for v1; if publishing fails the OS-level
    // log gets the diagnostic.
}

SyncServerBonjour::SyncServerBonjour() = default;

SyncServerBonjour::~SyncServerBonjour()
{
    stop();
}

bool SyncServerBonjour::publish(const juce::String& serviceName, int port)
{
    stop();

    NSString* name = [NSString stringWithUTF8String:
                        serviceName.toRawUTF8()];
    if (name == nil) name = @"Stylus";

    // Manual MRC: [alloc]/[init] returns a +1 retained object; we
    // hold that retain in impl_ and release once in stop(). The
    // project is built without ARC (no -fobjc-arc), so the
    // __bridge_retained / __bridge_transfer ownership-transfer
    // casts would compile to no-ops and the service would leak
    // or get prematurely deallocated.
    NSNetService* svc =
        [[NSNetService alloc] initWithDomain:@""
                                        type:@"_stylus-sync._tcp."
                                        name:name
                                        port:(int) port];
    if (svc == nil) return false;

    [svc scheduleInRunLoop:[NSRunLoop mainRunLoop]
                   forMode:NSRunLoopCommonModes];
    [svc publish];

    impl_ = (void*) svc;
    return true;
}

void SyncServerBonjour::stop()
{
    if (impl_ == nullptr) return;

    NSNetService* svc = (NSNetService*) impl_;
    impl_ = nullptr;

    [svc stop];
    [svc removeFromRunLoop:[NSRunLoop mainRunLoop]
                   forMode:NSRunLoopCommonModes];
    [svc release];
}

} // namespace Stylus
