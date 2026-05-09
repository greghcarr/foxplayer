// Windows implementation of the embedded-artwork bridge declared in
// AlbumArtExtractor.cpp. Uses the Windows Shell property system: every
// audio-format property handler shipped with Windows exposes embedded
// album art via PKEY_ThumbnailStream, which returns an IStream of the raw
// JPEG / PNG bytes. We just copy those bytes out and hand them back to the
// cross-platform wrapper, which feeds them into juce::ImageFileFormat.
//
// This covers MP3 (ID3v2 APIC), MP4 / M4A / AAC (covr atom), FLAC
// (PICTURE block), and WMA. Ogg Vorbis isn't reliably handled by the
// stock shell handler; the cross-platform fallback in AlbumArtExtractor.cpp
// (sidecar JPG / cover.jpg / folder.jpg) covers anything we miss.

#include <windows.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
// RAII wrapper for CoInitializeEx. Per-call to keep this bridge usable from
// any thread; if COM was already initialised (RPC_E_CHANGED_MODE), we leave
// it alone and skip the matching uninit.
struct ScopedCom
{
    bool needsUninit { false };
    ScopedCom()
    {
        const HRESULT hr = CoInitializeEx(nullptr,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        needsUninit = SUCCEEDED(hr);
    }
    ~ScopedCom()
    {
        if (needsUninit) CoUninitialize();
    }
};

// Reads the entire contents of an IStream into a malloc'd buffer. Returns
// nullptr on failure. *outSize is set to bytes read on success.
unsigned char* drainStream(IStream* stream, size_t* outSize)
{
    if (! stream) return nullptr;

    LARGE_INTEGER zero { 0 };
    if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) return nullptr;

    STATSTG stat {};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return nullptr;
    if (stat.cbSize.QuadPart == 0) return nullptr;
    // Sanity cap: an album-art image larger than 32 MB is almost certainly
    // a malformed handler returning bogus data; refuse rather than allocate
    // it and risk an OOM in a tight scanner loop.
    if (stat.cbSize.QuadPart > 32ull * 1024 * 1024) return nullptr;

    const size_t total = (size_t) stat.cbSize.QuadPart;
    auto* buf = (unsigned char*) std::malloc(total);
    if (! buf) return nullptr;

    // IStream::Read may return less than requested even from a memory stream,
    // so loop until we've drained the full size or hit a hard error.
    size_t consumed = 0;
    while (consumed < total)
    {
        ULONG got = 0;
        const HRESULT hr = stream->Read(buf + consumed,
                                        (ULONG) (total - consumed),
                                        &got);
        if (FAILED(hr)) { std::free(buf); return nullptr; }
        if (got == 0) break;
        consumed += got;
    }

    if (consumed == 0) { std::free(buf); return nullptr; }
    *outSize = consumed;
    return buf;
}
} // namespace

extern "C"
unsigned char* Stylus_extractEmbeddedArtwork(const char* utf8Path, size_t* outSize)
{
    if (outSize) *outSize = 0;
    if (! utf8Path || ! outSize) return nullptr;

    // UTF-8 -> UTF-16. Two-pass MultiByteToWideChar so we don't over-allocate.
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, nullptr, 0);
    if (wideLen <= 0) return nullptr;
    std::vector<wchar_t> widePath((size_t) wideLen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, widePath.data(), wideLen);

    ScopedCom com;

    // SHCreateItemFromParsingName -> IShellItem2 -> IPropertyStore.
    ComPtr<IShellItem2> item;
    if (FAILED(SHCreateItemFromParsingName(widePath.data(), nullptr,
                                           IID_PPV_ARGS(&item))))
        return nullptr;

    ComPtr<IPropertyStore> store;
    if (FAILED(item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARGS(&store))))
        return nullptr;

    // PKEY_ThumbnailStream is what Explorer asks for when generating the
    // thumbnail tile for a music file. The stock audio-format property
    // handlers populate it from embedded album art when present.
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (FAILED(store->GetValue(PKEY_ThumbnailStream, &pv))
        || pv.vt != VT_STREAM
        || pv.pStream == nullptr)
    {
        PropVariantClear(&pv);
        return nullptr;
    }

    // Take ownership of the stream out of the PROPVARIANT so PropVariantClear
    // doesn't release it before we read.
    ComPtr<IStream> stream;
    stream.Attach(pv.pStream);
    pv.pStream = nullptr;
    PropVariantClear(&pv);

    return drainStream(stream.Get(), outSize);
}
