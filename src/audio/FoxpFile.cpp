#include "FoxpFile.h"

namespace FoxPlayer
{

static constexpr int foxpVersion = 1;

juce::File FoxpFile::sidecarFor(const juce::File& audioFile)
{
    const juce::File parent = audioFile.getParentDirectory();
    const juce::String hidden = "." + audioFile.getFileName() + ".foxp";
    return parent.getChildFile(hidden);
}

bool FoxpFile::load(TrackInfo& track)
{
    const juce::File sidecar = sidecarFor(track.file);
    if (!sidecar.existsAsFile())
        return false;

    const juce::String text = sidecar.loadFileAsString();
    auto json = juce::JSON::parse(text);
    if (!json.isObject())
        return false;

    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return false;

    // User-editable metadata overrides
    if (obj->hasProperty("title"))       track.title       = obj->getProperty("title").toString();
    if (obj->hasProperty("artist"))      track.artist      = obj->getProperty("artist").toString();
    if (obj->hasProperty("album"))       track.album       = obj->getProperty("album").toString();
    if (obj->hasProperty("genre"))       track.genre       = obj->getProperty("genre").toString();
    if (obj->hasProperty("year"))        track.year        = obj->getProperty("year").toString();
    if (obj->hasProperty("trackNumber")) track.trackNumber = static_cast<int>(obj->getProperty("trackNumber"));

    // Analysis results
    if (obj->hasProperty("bpm"))
        track.bpm = static_cast<double>(obj->getProperty("bpm"));

    if (obj->hasProperty("key"))
        track.musicalKey = obj->getProperty("key").toString();

    if (obj->hasProperty("lufs"))
        track.lufs = static_cast<float>(static_cast<double>(obj->getProperty("lufs")));

    if (obj->hasProperty("hidden"))
        track.hidden = static_cast<bool>(obj->getProperty("hidden"));

    if (obj->hasProperty("playCount"))
        track.playCount = static_cast<int>(obj->getProperty("playCount"));

    return true;
}

bool FoxpFile::save(const TrackInfo& track)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", foxpVersion);

    // User-editable metadata overrides (only written if non-default)
    if (track.title.isNotEmpty())       obj->setProperty("title",       track.title);
    if (track.artist.isNotEmpty())      obj->setProperty("artist",      track.artist);
    if (track.album.isNotEmpty())       obj->setProperty("album",       track.album);
    if (track.genre.isNotEmpty())       obj->setProperty("genre",       track.genre);
    if (track.year.isNotEmpty())        obj->setProperty("year",        track.year);
    if (track.trackNumber > 0)          obj->setProperty("trackNumber", track.trackNumber);

    // Analysis results
    if (track.bpm > 0.0)
        obj->setProperty("bpm", track.bpm);

    if (track.musicalKey.isNotEmpty())
        obj->setProperty("key", track.musicalKey);

    if (track.lufs != 0.0f)
        obj->setProperty("lufs", static_cast<double>(track.lufs));

    if (track.hidden)
        obj->setProperty("hidden", true);

    if (track.playCount > 0)
        obj->setProperty("playCount", track.playCount);

    const juce::var root(obj);
    const juce::String text = juce::JSON::toString(root, false);

    const juce::File sidecar = sidecarFor(track.file);
    return sidecar.replaceWithText(text);
}

bool FoxpFile::exists(const TrackInfo& track)
{
    return sidecarFor(track.file).existsAsFile();
}

} // namespace FoxPlayer
