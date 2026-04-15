// Pure Objective-C++ — no JUCE headers. Returns a malloc'd buffer of raw image
// bytes (JPEG or PNG). Caller must free(). Returns nullptr if nothing is found.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <cstdlib>
#include <cstring>

extern "C"
unsigned char* FoxPlayer_extractEmbeddedArtwork(const char* utf8Path, size_t* outSize)
{
    *outSize = 0;

    @autoreleasepool
    {
        NSString* path  = [NSString stringWithUTF8String:utf8Path];
        AVAsset*  asset = [AVAsset assetWithURL:[NSURL fileURLWithPath:path]];

        NSArray<AVMetadataItem*>* items =
            [AVMetadataItem metadataItemsFromArray:asset.commonMetadata
                                          withKey:AVMetadataCommonKeyArtwork
                                         keySpace:AVMetadataKeySpaceCommon];

        for (AVMetadataItem* item in items)
        {
            NSData* data = nil;

            if ([item.value isKindOfClass:[NSData class]])
                data = (NSData*)item.value;
            else if ([item.value isKindOfClass:[NSDictionary class]])
                data = [(NSDictionary*)item.value objectForKey:@"data"];

            if (data.length > 0)
            {
                unsigned char* buf = (unsigned char*)std::malloc(data.length);
                if (!buf) return nullptr;
                std::memcpy(buf, data.bytes, data.length);
                *outSize = (size_t)data.length;
                return buf;
            }
        }
    }

    return nullptr;
}
