#pragma once

#include "../JuceCore.h"
#include "../audio/TrackInfo.h"
#include "../library/PlaylistStore.h"
#include "SyncPinManager.h"
#include <atomic>
#include <thread>
#include <functional>
#include <memory>
#include <vector>

namespace Stylus
{

// Minimal HTTP/1.1 server hosting the v1 Mac -> iPhone sync API.
// Owns one accept thread; each connection runs on its own worker
// thread so a slow /v1/file download doesn't block /v1/manifest or
// the next pair attempt. The body is hand-rolled HTTP because JUCE
// doesn't ship an HTTP server, the endpoint surface is tiny (four
// routes), and pulling in a dependency for this would be heavier
// than the parser itself.
//
// Lifecycle: caller flips on by calling start(); stop() blocks
// until the accept thread + every in-flight worker has unwound.
// Thread-safe to call start() / stop() repeatedly.
class SyncServer
{
public:
    using MusicFoldersProvider   = std::function<std::vector<juce::File>()>;
    using PodcastFoldersProvider = std::function<std::vector<juce::File>()>;
    using LibraryProvider        = std::function<std::vector<TrackInfo>()>;

    SyncServer(SyncPinManager&                pinManager,
               MusicFoldersProvider           musicFolders,
               PodcastFoldersProvider         podcastFolders,
               LibraryProvider                library,
               PlaylistStore&                 playlists);

    ~SyncServer();

    // Bind, advertise (caller's responsibility -- see
    // SyncServerBonjour for the NSNetService side), and start
    // accepting. Returns true on success. On failure the server
    // is left fully stopped and lastError() carries a message.
    bool start();
    void stop();

    bool isRunning() const  { return running_.load(); }
    int  boundPort() const  { return boundPort_;     }
    juce::String lastError() const { return lastError_; }

private:
    void acceptLoop();
    void handleConnection(std::unique_ptr<juce::StreamingSocket> sock);

    // HTTP parsing + dispatch helpers. Each handler writes the
    // full response into `sock`; failures fall through to a 500.
    struct Request
    {
        juce::String                                 method;
        juce::String                                 path;
        juce::String                                 query;
        juce::HashMap<juce::String, juce::String>    headers; // keys: lowercase
        juce::MemoryBlock                            body;
    };

    bool readRequest(juce::StreamingSocket& sock, Request& out);
    void writeStatusLineAndHeaders(juce::StreamingSocket& sock,
                                   int                    status,
                                   const juce::String&    contentType,
                                   juce::int64            contentLength,
                                   const juce::String&    extraHeaders = {});
    void writeJsonResponse(juce::StreamingSocket& sock,
                           int                    status,
                           const juce::String&    json);
    bool authorised(const Request& req);

    void handlePair       (juce::StreamingSocket&, const Request&);
    void handleManifest   (juce::StreamingSocket&, const Request&);
    void handlePlaylists  (juce::StreamingSocket&, const Request&);
    void handleFile       (juce::StreamingSocket&, const Request&);

    static juce::String queryParam(const juce::String& query,
                                   const juce::String& key);

    // Resolves ?root=music|podcast + ?rel=... to an absolute path
    // under the configured root, with traversal guards. Returns
    // an invalid juce::File on failure (empty rel, ".." segments,
    // missing root configuration).
    juce::File resolveFile(const juce::String& root,
                           const juce::String& rel);

    SyncPinManager&                pin_;
    MusicFoldersProvider           getMusicFolders_;
    PodcastFoldersProvider         getPodcastFolders_;
    LibraryProvider                getLibrary_;
    PlaylistStore&                 playlists_;

    std::unique_ptr<juce::StreamingSocket>   listener_;
    std::thread                              accepter_;
    std::atomic<bool>                        running_   { false };
    std::atomic<bool>                        stopFlag_  { false };

    int                                      boundPort_ { 0 };
    juce::String                             lastError_;

    // Each worker thread registers itself here on start and removes
    // itself on completion so stop() can wait for them to finish.
    std::mutex                               workersMutex_;
    std::vector<std::thread>                 workers_;
};

} // namespace Stylus
