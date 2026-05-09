#pragma once

// Windows-only Media Foundation audio reader. Plays back any format MF can
// decode through its built-in codecs — primarily AAC (.m4a/.mp4/.aac), Apple
// Lossless inside MP4, and WMA. JUCE's registerBasicFormats() on Windows
// covers MP3 / WAV / AIFF / FLAC / Ogg but not these, so without this bridge
// Apple-Music-derived libraries are unplayable.
//
// AudioEngine invokes createReaderForFile() as a fallback when the standard
// AudioFormatManager can't produce a reader; the returned object is a normal
// juce::AudioFormatReader subclass and behaves exactly like JUCE's built-in
// readers from then on (TransportSource, seek, BufferingAudioSource).

// Use the compiler-built-in _WIN32 for the file-level guard rather than
// JUCE_WINDOWS, which isn't defined until juce_core's preprocessor logic has
// run inside JuceHeader.h. The two flags are equivalent in effect.
#ifdef _WIN32

#include <JuceHeader.h>

namespace Stylus
{
namespace MediaFoundation
{
    // Returns true when MFStartup succeeded. Cached after first call. The
    // first invocation on a given process initialises the COM apartment we
    // need for MF; safe to call from the message thread before any reader
    // creation.
    bool ensureStarted();

    // Lightweight extension check: does this file's extension look like one
    // MF will likely be able to decode? Cheap, no I/O.
    bool canHandleExtension(const juce::String& extension);

    // Builds a Media Foundation source reader for the file and wraps it in a
    // juce::AudioFormatReader. Caller owns the returned pointer (return
    // nullptr signals "this file isn't ours / failed to open"; AudioEngine
    // treats that the same as JUCE returning nullptr).
    juce::AudioFormatReader* createReaderForFile(const juce::File& file);
}
} // namespace Stylus

#endif
