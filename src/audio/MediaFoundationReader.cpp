#ifdef _WIN32

#include "MediaFoundationReader.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

using Microsoft::WRL::ComPtr;

namespace Stylus
{
namespace MediaFoundation
{

namespace
{
// One-shot MFStartup/MFShutdown bracket. Safe to construct from any thread;
// MFStartup is documented to call CoInitialize internally for the calling
// thread, so the JUCE message thread (and any reader-creating thread) gets a
// COM apartment on first reach.
struct GlobalLifetime
{
    bool started { false };
    GlobalLifetime()
    {
        if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
            started = true;
    }
    ~GlobalLifetime()
    {
        if (started)
            MFShutdown();
    }
};

GlobalLifetime& lifetime()
{
    static GlobalLifetime g;
    return g;
}

// 100-ns tick units, the resolution Media Foundation uses for time stamps.
constexpr long long ticksPerSecond = 10'000'000LL;

// Convert sample index to MF 100-ns ticks at the given sample rate. Done in
// 64-bit to avoid overflow on tracks longer than ~50 minutes at 96 kHz.
inline long long samplesToTicks(juce::int64 samples, double sampleRate)
{
    return (long long) ((double) samples * (double) ticksPerSecond / sampleRate);
}

// AudioFormatReader subclass holding a configured IMFSourceReader.
class MFReader : public juce::AudioFormatReader
{
public:
    MFReader(ComPtr<IMFSourceReader> reader,
             unsigned int            channels,
             unsigned int            sampleRateHz,
             juce::int64             totalSamples)
        : juce::AudioFormatReader(nullptr, "Media Foundation"),
          reader_(std::move(reader))
    {
        sampleRate            = (double) sampleRateHz;
        bitsPerSample         = 32;
        usesFloatingPointData = true;
        numChannels           = channels;
        lengthInSamples       = totalSamples;
    }

    bool readSamples(int* const* destSamples, int numDestChannels,
                     int startOffsetInDestBuffer, juce::int64 startSampleInFile,
                     int numSamples) override
    {
        // The AudioFormatReader contract: write into destSamples (typed as
        // int* but holding floats when usesFloatingPointData=true) and zero
        // any channels we can't fill. JUCE's BufferingAudioSource calls us
        // sequentially in most cases, but seeks (e.g. on a manual scrub or
        // a new track load that resets cursor_ to 0) can jump backwards.
        if (numSamples <= 0) return true;

        // Backwards seek: ask MF to reposition. MF guarantees that the next
        // ReadSample after a seek lands at-or-before the requested timestamp,
        // so we may need to discard a leading slice; consumeTo() does that.
        if (startSampleInFile < cursor_ || startSampleInFile > cursor_ + bigSeekThreshold_)
        {
            if (! seekTo(startSampleInFile))
                return failAndZero(destSamples, numDestChannels,
                                   startOffsetInDestBuffer, numSamples);
            decodedBuffer_.clear();
            decodedSampleStart_ = startSampleInFile;
        }
        else if (startSampleInFile > cursor_)
        {
            // Small forward step: drain decoder until we hit the target.
            if (! consumeTo(startSampleInFile))
                return failAndZero(destSamples, numDestChannels,
                                   startOffsetInDestBuffer, numSamples);
        }

        cursor_ = startSampleInFile;

        int filled = 0;
        while (filled < numSamples)
        {
            if (decodedBuffer_.empty() || cursor_ >= decodedSampleStart_ + (juce::int64) framesInBuffer_)
            {
                if (! decodeNextChunk())
                {
                    // End of stream or hard failure: zero remainder and
                    // report success up to this point. JUCE treats a partial
                    // fill that's followed by zeros as natural EOF.
                    zeroRange(destSamples, numDestChannels,
                              startOffsetInDestBuffer + filled,
                              numSamples - filled);
                    return true;
                }
            }

            const juce::int64 offsetInBuffer = cursor_ - decodedSampleStart_;
            const int available = juce::jmax(0,
                (int) ((juce::int64) framesInBuffer_ - offsetInBuffer));
            if (available <= 0)
            {
                decodedBuffer_.clear();
                continue;
            }

            const int toCopy = juce::jmin(available, numSamples - filled);

            for (int ch = 0; ch < numDestChannels; ++ch)
            {
                if (destSamples[ch] == nullptr) continue;
                auto* dest = reinterpret_cast<float*> (destSamples[ch])
                             + startOffsetInDestBuffer + filled;

                if ((unsigned int) ch < numChannels)
                {
                    // MF gives us interleaved float; demux into JUCE's planar
                    // float channels.
                    const float* interleaved = decodedBuffer_.data()
                                             + (size_t) offsetInBuffer * numChannels
                                             + (size_t) ch;
                    for (int i = 0; i < toCopy; ++i)
                        dest[i] = interleaved[(size_t) i * numChannels];
                }
                else
                {
                    juce::FloatVectorOperations::clear(dest, toCopy);
                }
            }

            cursor_ += toCopy;
            filled  += toCopy;

            if (offsetInBuffer + toCopy >= (juce::int64) framesInBuffer_)
                decodedBuffer_.clear();
        }

        return true;
    }

private:
    bool seekTo(juce::int64 targetSample)
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt   = VT_I8;
        pv.hVal.QuadPart = samplesToTicks(targetSample, sampleRate);
        const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, pv);
        PropVariantClear(&pv);
        return SUCCEEDED(hr);
    }

    bool consumeTo(juce::int64 targetSample)
    {
        while (cursor_ < targetSample)
        {
            if (decodedBuffer_.empty()
                || cursor_ >= decodedSampleStart_ + (juce::int64) framesInBuffer_)
            {
                if (! decodeNextChunk()) return false;
            }
            cursor_ = juce::jmin(targetSample,
                                 decodedSampleStart_ + (juce::int64) framesInBuffer_);
            if (cursor_ >= decodedSampleStart_ + (juce::int64) framesInBuffer_)
                decodedBuffer_.clear();
        }
        return true;
    }

    bool decodeNextChunk()
    {
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(
            (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, nullptr, &streamFlags, &timestamp, &sample);

        if (FAILED(hr)) return false;
        if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
        if (streamFlags & MF_SOURCE_READERF_STREAMTICK) return decodeNextChunk(); // gap, retry
        if (sample == nullptr) return false; // timed out / no data

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;

        BYTE* raw   = nullptr;
        DWORD count = 0;
        if (FAILED(buffer->Lock(&raw, nullptr, &count))) return false;

        const size_t bytesPerFrame = sizeof(float) * (size_t) numChannels;
        framesInBuffer_ = (size_t) count / bytesPerFrame;

        decodedBuffer_.resize(framesInBuffer_ * (size_t) numChannels);
        std::memcpy(decodedBuffer_.data(), raw,
                    framesInBuffer_ * bytesPerFrame);

        buffer->Unlock();

        // Translate MF timestamp back to a sample index. ReadSample's
        // timestamp is the sample's start time in 100-ns ticks; convert with
        // the same ratio used in samplesToTicks().
        decodedSampleStart_ = (juce::int64)
            ((double) timestamp * sampleRate / (double) ticksPerSecond + 0.5);

        return true;
    }

    static bool failAndZero(int* const* destSamples, int numDestChannels,
                            int startOffsetInDestBuffer, int numSamples)
    {
        zeroRange(destSamples, numDestChannels, startOffsetInDestBuffer, numSamples);
        return false;
    }

    static void zeroRange(int* const* destSamples, int numDestChannels,
                          int startOffsetInDestBuffer, int numSamples)
    {
        for (int ch = 0; ch < numDestChannels; ++ch)
        {
            if (destSamples[ch] == nullptr) continue;
            juce::FloatVectorOperations::clear(
                reinterpret_cast<float*> (destSamples[ch]) + startOffsetInDestBuffer,
                numSamples);
        }
    }

    ComPtr<IMFSourceReader> reader_;

    // Decoded interleaved float chunk most recently returned by ReadSample,
    // plus the file-relative sample index of its first frame and its frame
    // count. Cleared when consumed.
    std::vector<float> decodedBuffer_;
    juce::int64        decodedSampleStart_ { 0 };
    size_t             framesInBuffer_     { 0 };

    // Last sample-index served. Used to spot backwards or large-forward seeks
    // that warrant a SetCurrentPosition call rather than a drain loop.
    juce::int64        cursor_ { 0 };

    // Past this many forward samples, prefer a hard seek over draining the
    // decoder (avoids reading and discarding minutes of audio when the user
    // scrubs the seek bar).
    static constexpr juce::int64 bigSeekThreshold_ = 192'000; // ~2 sec @ 96k
};

bool configureFloatOutput(IMFSourceReader* reader,
                          unsigned int& outChannels,
                          unsigned int& outSampleRate)
{
    // Deselect everything but the first audio stream so MF only decodes audio.
    reader->SetStreamSelection((DWORD) MF_SOURCE_READER_ALL_STREAMS,        FALSE);
    reader->SetStreamSelection((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    // Ask MF for 32-bit float PCM at the source's native rate / channel
    // layout. MF's resampler is invoked automatically when needed.
    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type))) return false;
    type->SetGUID(MF_MT_MAJOR_TYPE,    MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE,       MFAudioFormat_Float);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

    if (FAILED(reader->SetCurrentMediaType(
            (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            nullptr, type.Get())))
        return false;

    // Pull resolved attributes back so the reader knows the actual rate /
    // channel count MF settled on.
    ComPtr<IMFMediaType> resolved;
    if (FAILED(reader->GetCurrentMediaType(
            (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &resolved)))
        return false;

    UINT32 ch = 0, sr = 0;
    if (FAILED(resolved->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,        &ch))) return false;
    if (FAILED(resolved->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr))) return false;

    outChannels   = ch;
    outSampleRate = sr;
    return true;
}

} // namespace

bool ensureStarted()
{
    return lifetime().started;
}

bool canHandleExtension(const juce::String& extension)
{
    auto e = extension.toLowerCase();
    if (e.startsWith(".")) e = e.substring(1);
    return e == "m4a" || e == "mp4" || e == "aac"
        || e == "alac" || e == "m4b" || e == "m4r"
        || e == "wma";
}

juce::AudioFormatReader* createReaderForFile(const juce::File& file)
{
    if (! ensureStarted()) return nullptr;
    if (! canHandleExtension(file.getFileExtension())) return nullptr;
    if (! file.existsAsFile()) return nullptr;

    ComPtr<IMFSourceReader> reader;
    const HRESULT hr = MFCreateSourceReaderFromURL(
        file.getFullPathName().toWideCharPointer(),
        nullptr, &reader);
    if (FAILED(hr)) return nullptr;

    unsigned int channels = 0, sampleRate = 0;
    if (! configureFloatOutput(reader.Get(), channels, sampleRate))
        return nullptr;
    if (channels == 0 || sampleRate == 0) return nullptr;

    // Total length: prefer the source's MF_PD_DURATION attribute, which is
    // the container-level duration in 100-ns ticks. Fall back to 0 (unknown)
    // if MF can't provide it; the transport then plays until EOS naturally.
    juce::int64 lengthInSamples = 0;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(reader->GetPresentationAttribute(
            (DWORD) MF_SOURCE_READER_MEDIASOURCE,
            MF_PD_DURATION, &pv)))
    {
        if (pv.vt == VT_UI8)
            lengthInSamples = (juce::int64)
                ((double) pv.uhVal.QuadPart * sampleRate / (double) ticksPerSecond + 0.5);
        else if (pv.vt == VT_I8)
            lengthInSamples = (juce::int64)
                ((double) pv.hVal.QuadPart * sampleRate / (double) ticksPerSecond + 0.5);
        PropVariantClear(&pv);
    }

    return new MFReader(std::move(reader), channels, sampleRate, lengthInSamples);
}

} // namespace MediaFoundation
} // namespace Stylus

#endif
