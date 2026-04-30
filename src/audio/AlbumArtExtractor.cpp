#include "AlbumArtExtractor.h"
#include <cstdlib>

// Declared in AlbumArtExtractor.mm, no JUCE types cross the boundary.
extern "C"
unsigned char* Stylus_extractEmbeddedArtwork(const char* utf8Path, size_t* outSize);

namespace Stylus
{

juce::Image AlbumArtExtractor::extractFromFile(const juce::File& file)
{
    // Try embedded artwork via AVFoundation.
    size_t size = 0;
    unsigned char* raw = Stylus_extractEmbeddedArtwork(
        file.getFullPathName().toRawUTF8(), &size);

    if (raw && size > 0)
    {
        juce::MemoryBlock mb(raw, size);
        std::free(raw);
        juce::MemoryInputStream mis(mb, false);
        auto img = juce::ImageFileFormat::loadFrom(mis);
        if (img.isValid()) return img;
    }
    else if (raw)
    {
        std::free(raw);
    }

    // Per-track sidecar art written by the Apple Music lookup task.
    {
        const juce::File parent = file.getParentDirectory();
        const juce::File art    = parent.getChildFile("." + file.getFileName() + ".styl-art.jpg");
        if (art.existsAsFile())
        {
            auto img = juce::ImageFileFormat::loadFrom(art);
            if (img.isValid()) return img;
        }
    }

    // Fall back to common cover art filenames in the same directory.
    const juce::File dir = file.getParentDirectory();
    static const char* names[] = { "cover", "folder", "artwork", "album", "front", nullptr };
    static const char* exts[]  = { ".jpg", ".jpeg", ".png", nullptr };

    for (int ni = 0; names[ni] != nullptr; ++ni)
    {
        for (int ei = 0; exts[ei] != nullptr; ++ei)
        {
            const juce::File candidate = dir.getChildFile(
                juce::String(names[ni]) + exts[ei]);
            if (candidate.existsAsFile())
            {
                auto img = juce::ImageFileFormat::loadFrom(candidate);
                if (img.isValid()) return img;
            }
        }
    }

    return {};
}

} // namespace Stylus
