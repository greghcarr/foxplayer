// Windows implementation of the embedded-artwork bridge declared in
// AlbumArtExtractor.cpp. Returns nullptr for now: the cross-platform wrapper
// in AlbumArtExtractor.cpp falls back to per-track sidecar art and to common
// cover-art filenames in the same directory, so most libraries get art
// without embedded extraction. A future TagLib or Windows Property System
// integration can replace this stub.

#include <cstddef>

extern "C"
unsigned char* Stylus_extractEmbeddedArtwork(const char* /*utf8Path*/, size_t* outSize)
{
    if (outSize) *outSize = 0;
    return nullptr;
}
