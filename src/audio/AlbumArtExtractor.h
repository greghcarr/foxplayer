#pragma once

#include "TrackInfo.h"
#include "JuceCore.h"
#include <juce_graphics/juce_graphics.h>

namespace Stylus
{

class AlbumArtExtractor
{
public:
    // Returns the album art for the given track, or an invalid Image if none is found.
    // First tries embedded artwork via AVFoundation, then looks for cover/folder/artwork
    // image files in the same directory.
    static juce::Image extractFromFile(const juce::File& file);
};

} // namespace Stylus
