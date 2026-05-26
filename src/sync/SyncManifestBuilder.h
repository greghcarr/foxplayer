#pragma once

#include "../JuceCore.h"
#include "../audio/TrackInfo.h"
#include "../library/PlaylistStore.h"

namespace Stylus
{

// Pure functions that walk the desktop's current library state +
// playlists and serialise them into JSON wire-format payloads for
// the iOS SyncClient. The shape MUST stay in lock-step with
// Sources/StylusApp/Sync/SyncManifest.swift on the iOS side -- any
// field change ships in both halves of the same release.
//
// All sidecar sizes are looked up via juce::File::getSize(); 0
// (which JUCE also returns for non-existent files) is interpreted
// as "no sidecar to ship" and elides the corresponding field.

class SyncManifestBuilder
{
public:
    // GET /v1/manifest. roots are the user's configured music and
    // podcast folder roots (may be more than one of each; the
    // builder picks the deepest matching root for each track so
    // shadowed-track paths -- a track that sits under both a music
    // and a podcast root -- are routed by the podcast role).
    static juce::String buildManifest(
        const std::vector<juce::File>&     musicFolders,
        const std::vector<juce::File>&     podcastFolders,
        const std::vector<TrackInfo>&      library);

    // GET /v1/playlists. Rewrites every absolute trackPath in the
    // PlaylistStore as music-root-relative POSIX. Tracks whose path
    // doesn't fall under any music root are dropped silently --
    // they wouldn't resolve on the iPhone either way.
    static juce::String buildPlaylists(
        const std::vector<juce::File>&     musicFolders,
        const PlaylistStore&               playlists);
};

} // namespace Stylus
