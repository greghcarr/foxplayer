#include "SyncServer.h"
#include "SyncManifestBuilder.h"

namespace Stylus
{

namespace
{
    constexpr int kStreamChunkBytes  = 64 * 1024;
    constexpr int kMaxBodyBytes      = 16 * 1024;   // /v1/pair only

    juce::String headerKey(juce::String s)
    {
        s = s.trim();
        return s.toLowerCase();
    }

    bool readUntilDoubleCRLF(juce::StreamingSocket& sock,
                             juce::String&           headerBlock)
    {
        // Read into a memory buffer one byte at a time until \r\n\r\n.
        // Header sizes are tiny so the per-byte read cost is fine.
        // shouldBlock = true so each call waits for one byte rather
        // than returning 0 the instant the kernel buffer is empty.
        // The previous shouldBlock = false treated "no data yet" the
        // same as EOF, so we'd drop the connection as soon as the
        // headers arrived in more than one TCP packet (which is
        // every iOS request).
        juce::MemoryBlock buf;
        char b = 0;
        int run = 0;
        while (run < 8 * 1024)
        {
            const auto n = sock.read(&b, 1, true);
            if (n <= 0) return false;
            buf.append(&b, 1);
            ++run;
            if (buf.getSize() >= 4)
            {
                const auto* p = static_cast<const char*>(buf.getData())
                              + buf.getSize() - 4;
                if (p[0] == '\r' && p[1] == '\n'
                 && p[2] == '\r' && p[3] == '\n')
                {
                    headerBlock = juce::String::createStringFromData(
                        buf.getData(), (int) buf.getSize() - 4);
                    return true;
                }
            }
        }
        return false;
    }

    bool readNBytes(juce::StreamingSocket& sock,
                    juce::MemoryBlock&     out,
                    int                    n)
    {
        // shouldBlock = true (same reasoning as readUntilDoubleCRLF).
        // JUCE will block until either n bytes have been read or the
        // connection is closed; we then loop in case the read came
        // back short.
        out.setSize((size_t) n);
        int got = 0;
        while (got < n)
        {
            const auto r = sock.read((char*) out.getData() + got,
                                     n - got,
                                     true);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }

    juce::String urlDecode(const juce::String& s)
    {
        return juce::URL::removeEscapeChars(s);
    }
}

SyncServer::SyncServer(SyncPinManager&                pinManager,
                       MusicFoldersProvider           musicFolders,
                       PodcastFoldersProvider         podcastFolders,
                       LibraryProvider                library,
                       PlaylistStore&                 playlists)
    : pin_              (pinManager),
      getMusicFolders_  (std::move(musicFolders)),
      getPodcastFolders_(std::move(podcastFolders)),
      getLibrary_       (std::move(library)),
      playlists_        (playlists)
{}

SyncServer::~SyncServer()
{
    stop();
}

bool SyncServer::start()
{
    if (running_.load()) return true;

    listener_ = std::make_unique<juce::StreamingSocket>();
    if (! listener_->createListener(0))   // 0 = OS-picked port
    {
        lastError_ = "Couldn't open a listening socket.";
        listener_.reset();
        return false;
    }
    boundPort_ = listener_->getBoundPort();
    stopFlag_.store(false);
    running_.store(true);

    accepter_ = std::thread([this] { acceptLoop(); });
    return true;
}

void SyncServer::stop()
{
    if (! running_.load()) return;

    stopFlag_.store(true);
    if (listener_) listener_->close();
    if (accepter_.joinable()) accepter_.join();

    // Wait for every per-connection worker to finish. We don't
    // forcibly close their sockets -- they already received a
    // stop signal from listener_->close() via failed reads.
    std::vector<std::thread> toJoin;
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        toJoin = std::move(workers_);
        workers_.clear();
    }
    for (auto& t : toJoin)
        if (t.joinable()) t.join();

    listener_.reset();
    running_.store(false);
    boundPort_ = 0;

    pin_.revokeAllTokens();
}

void SyncServer::acceptLoop()
{
    while (! stopFlag_.load() && listener_ && listener_->isConnected())
    {
        auto* raw = listener_->waitForNextConnection();
        if (raw == nullptr)
        {
            // Listener closed under us (stop() path) or no incoming
            // within the wait window. Loop again unless we're done.
            continue;
        }
        std::unique_ptr<juce::StreamingSocket> sock { raw };

        std::lock_guard<std::mutex> lock(workersMutex_);
        workers_.emplace_back([this, s = std::move(sock)]() mutable
        {
            handleConnection(std::move(s));
        });
    }
}

void SyncServer::handleConnection(std::unique_ptr<juce::StreamingSocket> sock)
{
    Request req;
    if (! readRequest(*sock, req))
        return;

    if (req.method == "POST" && req.path == "/v1/pair")
        handlePair      (*sock, req);
    else if (req.method == "GET" && req.path == "/v1/manifest")
        handleManifest  (*sock, req);
    else if (req.method == "GET" && req.path == "/v1/playlists")
        handlePlaylists (*sock, req);
    else if (req.method == "GET" && req.path == "/v1/file")
        handleFile      (*sock, req);
    else
        writeJsonResponse(*sock, 404, "{\"error\":\"notFound\"}");
}

bool SyncServer::readRequest(juce::StreamingSocket& sock, Request& out)
{
    juce::String headerBlock;
    if (! readUntilDoubleCRLF(sock, headerBlock))
        return false;

    auto lines = juce::StringArray::fromTokens(headerBlock, "\r\n", "");
    if (lines.size() < 1) return false;

    // Request line: METHOD PATH?QUERY HTTP/1.1
    auto reqLine = lines[0];
    auto parts   = juce::StringArray::fromTokens(reqLine, " ", "");
    if (parts.size() < 3) return false;
    out.method = parts[0];

    auto fullPath = parts[1];
    const auto q  = fullPath.indexOfChar('?');
    if (q >= 0)
    {
        out.path  = fullPath.substring(0, q);
        out.query = fullPath.substring(q + 1);
    }
    else
    {
        out.path  = fullPath;
        out.query = "";
    }

    for (int i = 1; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        const auto colon = line.indexOfChar(':');
        if (colon <= 0) continue;
        out.headers.set(
            headerKey(line.substring(0, colon)),
            line.substring(colon + 1).trim()
        );
    }

    const auto cl = out.headers["content-length"].getIntValue();
    if (cl > 0)
    {
        if (cl > kMaxBodyBytes) return false;
        if (! readNBytes(sock, out.body, cl)) return false;
    }
    return true;
}

void SyncServer::writeStatusLineAndHeaders(juce::StreamingSocket& sock,
                                           int                    status,
                                           const juce::String&    contentType,
                                           juce::int64            contentLength,
                                           const juce::String&    extraHeaders)
{
    juce::String head;
    head << "HTTP/1.1 " << status << " " << (status == 200 ? "OK"
                                          : status == 206 ? "Partial Content"
                                          : status == 401 ? "Unauthorized"
                                          : status == 404 ? "Not Found"
                                          : status == 416 ? "Range Not Satisfiable"
                                          : status == 500 ? "Server Error"
                                          : "");
    head << "\r\n";
    head << "Content-Type: " << contentType << "\r\n";
    head << "Content-Length: " << contentLength << "\r\n";
    head << "Connection: close\r\n";
    if (extraHeaders.isNotEmpty()) head << extraHeaders;
    head << "\r\n";
    const auto utf = head.toUTF8();
    sock.write(utf.getAddress(), (int) std::strlen(utf.getAddress()));
}

void SyncServer::writeJsonResponse(juce::StreamingSocket& sock,
                                   int                    status,
                                   const juce::String&    json)
{
    const auto utf = json.toUTF8();
    const auto len = (juce::int64) std::strlen(utf.getAddress());
    writeStatusLineAndHeaders(sock, status, "application/json", len);
    sock.write(utf.getAddress(), (int) len);
}

bool SyncServer::authorised(const Request& req)
{
    auto auth = req.headers["authorization"];
    if (! auth.startsWith("Bearer ")) return false;
    return pin_.isValidToken(auth.substring(7));
}

void SyncServer::handlePair(juce::StreamingSocket& sock, const Request& req)
{
    juce::String pin;
    {
        const juce::String bodyStr {
            (const char*) req.body.getData(),
            (size_t) req.body.getSize()
        };
        auto parsed = juce::JSON::parse(bodyStr);
        if (parsed.isObject())
            pin = parsed.getProperty("pin", "").toString();
    }

    juce::String token;
    const auto r = pin_.validatePin(pin, token);
    switch (r)
    {
    case SyncPinManager::PairResult::Ok:
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("token", token);
        writeJsonResponse(sock, 200, juce::JSON::toString(juce::var(obj)));
        return;
    }
    case SyncPinManager::PairResult::LockedOut:
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("code",    "lockedOut");
        obj->setProperty("message",
            "Too many failed attempts. Wait a minute and try again.");
        writeJsonResponse(sock, 401, juce::JSON::toString(juce::var(obj)));
        return;
    }
    case SyncPinManager::PairResult::WrongPin:
    default:
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("code",    "wrongPin");
        obj->setProperty("message", "Wrong PIN.");
        writeJsonResponse(sock, 401, juce::JSON::toString(juce::var(obj)));
        return;
    }
    }
}

void SyncServer::handleManifest(juce::StreamingSocket& sock, const Request& req)
{
    if (! authorised(req))
    {
        writeJsonResponse(sock, 401, "{\"error\":\"unauthorized\"}");
        return;
    }
    const auto json = SyncManifestBuilder::buildManifest(
        getMusicFolders_(), getPodcastFolders_(), getLibrary_());
    writeJsonResponse(sock, 200, json);
}

void SyncServer::handlePlaylists(juce::StreamingSocket& sock, const Request& req)
{
    if (! authorised(req))
    {
        writeJsonResponse(sock, 401, "{\"error\":\"unauthorized\"}");
        return;
    }
    const auto json = SyncManifestBuilder::buildPlaylists(
        getMusicFolders_(), playlists_);
    writeJsonResponse(sock, 200, json);
}

void SyncServer::handleFile(juce::StreamingSocket& sock, const Request& req)
{
    if (! authorised(req))
    {
        writeJsonResponse(sock, 401, "{\"error\":\"unauthorized\"}");
        return;
    }

    const auto root = urlDecode(queryParam(req.query, "root"));
    const auto rel  = urlDecode(queryParam(req.query, "rel"));
    const auto file = resolveFile(root, rel);
    if (! file.existsAsFile())
    {
        writeJsonResponse(sock, 404, "{\"error\":\"notFound\"}");
        return;
    }

    const auto totalLen = (juce::int64) file.getSize();
    juce::int64 startOffset = 0;
    juce::int64 endOffset   = totalLen - 1;
    bool partial = false;

    const auto rangeHdr = req.headers["range"];
    if (rangeHdr.startsWith("bytes="))
    {
        const auto spec = rangeHdr.substring(6);
        const auto dash = spec.indexOfChar('-');
        if (dash > 0)
        {
            const juce::int64 s = spec.substring(0, dash).getLargeIntValue();
            const auto endSpec  = spec.substring(dash + 1);
            const juce::int64 e =
                endSpec.isEmpty() ? totalLen - 1
                                  : endSpec.getLargeIntValue();
            if (s >= 0 && s < totalLen && e >= s && e < totalLen)
            {
                startOffset = s;
                endOffset   = e;
                partial     = true;
            }
            else
            {
                juce::String extra;
                extra << "Content-Range: bytes */" << totalLen << "\r\n";
                writeStatusLineAndHeaders(sock, 416,
                                           "application/json", 0, extra);
                return;
            }
        }
    }

    const auto contentLen = endOffset - startOffset + 1;
    juce::String extra;
    if (partial)
    {
        extra << "Content-Range: bytes "
              << startOffset << "-" << endOffset << "/" << totalLen << "\r\n";
    }
    writeStatusLineAndHeaders(sock, partial ? 206 : 200,
                              "application/octet-stream",
                              contentLen, extra);

    // Stream the file body in chunks.
    juce::FileInputStream in(file);
    if (in.failedToOpen())
    {
        // Can't recover meaningfully -- the body length we promised
        // won't match. Drop the connection.
        return;
    }
    in.setPosition(startOffset);

    juce::HeapBlock<char> buf((size_t) kStreamChunkBytes);
    juce::int64 remaining = contentLen;
    while (remaining > 0)
    {
        if (stopFlag_.load()) return;
        const int want = (int) std::min<juce::int64>(remaining,
                                                     kStreamChunkBytes);
        const int got  = in.read(buf.getData(), want);
        if (got <= 0) return;
        const int wrote = sock.write(buf.getData(), got);
        if (wrote != got) return;
        remaining -= got;
    }
}

juce::String SyncServer::queryParam(const juce::String& query,
                                    const juce::String& key)
{
    if (query.isEmpty()) return {};
    auto pairs = juce::StringArray::fromTokens(query, "&", "");
    for (const auto& p : pairs)
    {
        const auto eq = p.indexOfChar('=');
        if (eq <= 0) continue;
        if (p.substring(0, eq) == key)
            return p.substring(eq + 1);
    }
    return {};
}

juce::File SyncServer::resolveFile(const juce::String& root,
                                   const juce::String& rel)
{
    if (rel.isEmpty() || rel.contains("..")
                      || rel.startsWithChar('/')
                      || rel.startsWithChar('\\'))
        return {};

    const auto folders =
        (root == "music"   ? getMusicFolders_()
       : root == "podcast" ? getPodcastFolders_()
                           : std::vector<juce::File>{});
    if (folders.empty()) return {};

    // The manifest path is root-relative; we try each configured
    // root in order and return the first that contains a matching
    // file. With a single root (the common case) this resolves on
    // the first hit.
    for (const auto& base : folders)
    {
        const auto f = base.getChildFile(rel);
        if (f.existsAsFile() && f.isAChildOf(base))
            return f;
    }
    return {};
}

} // namespace Stylus
