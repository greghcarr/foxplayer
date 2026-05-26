#include "SyncManifestBuilder.h"

namespace Stylus
{

namespace
{
    // Returns the deepest folder in `roots` that contains `file`,
    // or an invalid juce::File if none match. "Deepest" matters
    // when the user's music root nests a podcast root (or vice
    // versa) -- we want the more specific role to win.
    juce::File deepestRoot(const juce::File&                  file,
                           const std::vector<juce::File>&     roots)
    {
        juce::File best;
        for (const auto& r : roots)
        {
            if (file.isAChildOf(r) || file == r)
            {
                if (! best.exists()
                    || r.getFullPathName().length() > best.getFullPathName().length())
                {
                    best = r;
                }
            }
        }
        return best;
    }

    juce::String relativePosix(const juce::File& file, const juce::File& root)
    {
        auto rel = file.getRelativePathFrom(root);
        // juce::File::getRelativePathFrom returns OS-native; the
        // wire format is POSIX (matches iOS-side path semantics).
        rel = rel.replaceCharacter('\\', '/');
        if (rel.startsWith("./")) rel = rel.substring(2);
        return rel;
    }

    juce::File sidecarStyl(const juce::File& audio)
    {
        return audio.getParentDirectory().getChildFile(
            "." + audio.getFileName() + ".styl");
    }

    juce::File sidecarArt(const juce::File& audio)
    {
        return audio.getParentDirectory().getChildFile(
            "." + audio.getFileName() + ".styl-art.jpg");
    }

    juce::var entryFor(const juce::File& audio,
                       const juce::File& root,
                       juce::int64&      totalBytesAccumulator)
    {
        auto* obj = new juce::DynamicObject();

        const auto rel  = relativePosix(audio, root);
        const auto size = (juce::int64) audio.getSize();
        obj->setProperty("rel",  rel);
        obj->setProperty("size", size);
        totalBytesAccumulator += size;

        const auto styl = sidecarStyl(audio);
        if (styl.existsAsFile())
        {
            const auto sz = (juce::int64) styl.getSize();
            obj->setProperty("styl",     styl.getFileName());
            obj->setProperty("stylSize", sz);
            totalBytesAccumulator += sz;
        }
        const auto art = sidecarArt(audio);
        if (art.existsAsFile())
        {
            const auto sz = (juce::int64) art.getSize();
            obj->setProperty("art",     art.getFileName());
            obj->setProperty("artSize", sz);
            totalBytesAccumulator += sz;
        }
        return juce::var(obj);
    }
}

juce::String SyncManifestBuilder::buildManifest(
    const std::vector<juce::File>&  musicFolders,
    const std::vector<juce::File>&  podcastFolders,
    const std::vector<TrackInfo>&   library)
{
    juce::Array<juce::var> musicEntries;
    juce::Array<juce::var> podcastEntries;
    juce::int64 totalBytes = 0;

    for (const auto& t : library)
    {
        if (! t.file.existsAsFile()) continue;

        // Route by which set of roots claims the file. If a track
        // sits under both a music and a podcast root, the deepest
        // (most-specific) folder wins -- and if both ties produce
        // the same depth, the explicit isPodcast flag on TrackInfo
        // (set by the scanner) breaks the tie.
        const auto music   = deepestRoot(t.file, musicFolders);
        const auto podcast = deepestRoot(t.file, podcastFolders);

        const bool prefersPodcast =
            t.isPodcast
         && podcast.exists()
         && podcast.getFullPathName().length()
                >= (music.exists() ? music.getFullPathName().length() : 0);

        if (prefersPodcast)
        {
            podcastEntries.add(entryFor(t.file, podcast, totalBytes));
        }
        else if (music.exists())
        {
            musicEntries.add(entryFor(t.file, music, totalBytes));
        }
        // Track not under any configured root -> drop silently;
        // the iPhone has no way to place it on its end.
    }

    // Peer block. Hostname is the user-facing computer name so the
    // iPhone's discovery list shows something recognisable.
    auto* peer = new juce::DynamicObject();
    peer->setProperty("hostname",
        juce::SystemStats::getComputerName());
    peer->setProperty("appVersion",
        juce::String(JUCE_STRINGIFY(JUCE_APPLICATION_VERSION_STRING)));

    auto* root = new juce::DynamicObject();
    root->setProperty("version",    1);
    root->setProperty("peer",       juce::var(peer));
    root->setProperty("music",      musicEntries);
    root->setProperty("podcasts",   podcastEntries);
    root->setProperty("totalBytes", totalBytes);

    return juce::JSON::toString(juce::var(root));
}

juce::String SyncManifestBuilder::buildPlaylists(
    const std::vector<juce::File>&  musicFolders,
    const PlaylistStore&            playlists)
{
    juce::Array<juce::var> arr;
    for (const auto& p : playlists.all())
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id",   p.id);
        obj->setProperty("name", p.name);

        juce::Array<juce::var> paths;
        for (const auto& pathStr : p.trackPaths)
        {
            const juce::File f { pathStr };
            const auto root = deepestRoot(f, musicFolders);
            if (! root.exists()) continue;     // out-of-tree -> drop
            paths.add(relativePosix(f, root));
        }
        obj->setProperty("trackPaths", paths);
        arr.add(juce::var(obj));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("version",   1);
    root->setProperty("playlists", arr);
    return juce::JSON::toString(juce::var(root));
}

} // namespace Stylus
