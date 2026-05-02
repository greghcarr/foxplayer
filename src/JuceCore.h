#pragma once

// Replacement for <JuceHeader.h> in StylusCore-bound sources. Pulls in only
// the GUI-free JUCE modules so that core code compiles in environments
// without juce_graphics / juce_gui_basics linked (e.g. the iOS port's
// StylusCore static library underneath a SwiftUI app).
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
