// xcloud_client.cpp: Xbox remote play and xCloud. Sign in, browse, session,
// and the WebRTC transport all live here.
//
// Auth is a chain: device code login gives an MS token, that trades up to an
// Xbox Live user token, then XSTS for the streaming relying party, and finally
// a stream token per offering (xhome for your consoles, xgpuweb for cloud). The
// stream token carries the region host and bearer.

#include "app_paths.h"
#include "xcloud_client.h"
#include "http_util.h"
#include "json_util.h"
#include "client_util.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <set>
#include <map>
#include <sys/stat.h>

#if defined(THESEUS_HAVE_WEBRTC)
#include <rtc/rtc.hpp>
#include "xcloud_video.h"
#include "xcloud_audio.h"
#endif

// Persisted MS refresh token, so login is a one time thing. Lives in the
// desktop ini next to the Plex and Jellyfin tokens.
extern char g_xcloudRefreshToken[2048];
extern void SaveDesktopSettings();

// ============================================================================
// Constants (the values Greenlight / xal-node use)
// ============================================================================

static const char* kClientId = "1f907974-e22b-4810-a9de-d9647380c97e";
static const char* kScope    = "xboxlive.signin openid profile offline_access";
static const char* kDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
static const char* kTokenUrl      = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static const char* kXblUrl        = "https://user.auth.xboxlive.com/user/authenticate";
static const char* kXstsUrl       = "https://xsts.auth.xboxlive.com/xsts/authorize";
static const char* kGssvRelying   = "http://gssv.xboxlive.com/";

// ============================================================================
// The token chain
// ============================================================================

struct StreamEndpoint {
    std::string host;   // region base uri, e.g. https://wus.gssv-play-prod.xboxlive.com
    std::string token;  // gsToken bearer
    bool ok() const { return !host.empty() && !token.empty(); }
};

static std::string XblJson(const std::string& msToken)
{
    return std::string("{\"Properties\":{\"AuthMethod\":\"RPS\",\"RpsTicket\":\"d=")
        + msToken + "\",\"SiteName\":\"user.auth.xboxlive.com\"},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
}

static std::string XstsJson(const std::string& userToken)
{
    return std::string("{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"")
        + userToken + "\"]},\"RelyingParty\":\"" + kGssvRelying + "\",\"TokenType\":\"JWT\"}";
}

// Trade the refresh token for a fresh MS access token. Returns "" on failure
// and rewrites g_xcloudRefreshToken with the rotated refresh token.
static std::string RefreshMsToken()
{
    if (g_xcloudRefreshToken[0] == 0) return "";
    std::string body = std::string("grant_type=refresh_token&client_id=") + kClientId +
        "&refresh_token=" + g_xcloudRefreshToken + "&scope=" + "xboxlive.signin%20openid%20profile%20offline_access";
    HttpHeaders h = { { "Content-Type", "application/x-www-form-urlencoded" } };
    HttpResponse r = Http_Post(kTokenUrl, body, h);
    if (!r.ok()) return "";
    std::string rt = Json_GetString(r.body, "refresh_token");
    if (!rt.empty()) {
        strncpy(g_xcloudRefreshToken, rt.c_str(), sizeof(g_xcloudRefreshToken) - 1);
        g_xcloudRefreshToken[sizeof(g_xcloudRefreshToken) - 1] = 0;
        SaveDesktopSettings();
    }
    return Json_GetString(r.body, "access_token");
}

// The session /connect step wants a Passport "console transfer" token, minted
// from the same refresh token but at the legacy live endpoint with a special
// scope. Distinct from the access token used everywhere else.
static std::string GetPassportToken()
{
    if (g_xcloudRefreshToken[0] == 0) return "";
    std::string body = std::string("client_id=") + kClientId +
        "&scope=service::http://Passport.NET/purpose::PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN"
        "&grant_type=refresh_token&refresh_token=" + g_xcloudRefreshToken;
    HttpHeaders h = { { "Content-Type", "application/x-www-form-urlencoded" } };
    HttpResponse r = Http_Post("https://login.live.com/oauth20_token.srf", body, h);
    if (!r.ok()) {
        fprintf(stderr, "[xCloud] passport token HTTP %ld body=%.200s\n", r.status, r.body.c_str());
        return "";
    }
    return Json_GetString(r.body, "access_token");
}

static const HttpHeaders kXblHeaders = {
    { "Content-Type", "application/json" },
    { "Accept", "application/json" },
    { "x-xbl-contract-version", "1" },
};

// MS token to XBL user token. Returns "" on failure.
static std::string GetXblUserToken(const std::string& msToken)
{
    HttpResponse r = Http_Post(kXblUrl, XblJson(msToken), kXblHeaders);
    if (!r.ok()) return "";
    return Json_GetString(r.body, "Token");
}

// XBL user token to XSTS token for the streaming relying party. Also pulls the
// gamertag out on the way through.
static std::string GetGssvToken(const std::string& userToken, std::string& gamertagOut)
{
    HttpResponse r = Http_Post(kXstsUrl, XstsJson(userToken), kXblHeaders);
    if (!r.ok()) return "";
    std::string gtg = Json_GetString(r.body, "gtg");
    if (!gtg.empty()) gamertagOut = gtg;
    return Json_GetString(r.body, "Token");
}

// XSTS token to stream endpoint for one offering (xhome / xgpuweb / xgpuwebf2p).
static StreamEndpoint GetStreamEndpoint(const std::string& gssvToken, const char* offering)
{
    StreamEndpoint ep;
    std::string url = std::string("https://") + offering + ".gssv-play-prod.xboxlive.com/v2/login/user";
    std::string body = std::string("{\"token\":\"") + gssvToken + "\",\"offeringId\":\"" + offering + "\"}";
    HttpHeaders h = {
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
        { "x-gssv-client", "XboxComBrowser" },
    };
    HttpResponse r = Http_Post(url, body, h);
    if (!r.ok()) return ep;
    ep.token = Json_GetString(r.body, "gsToken");
    // First region's baseUri is good enough for browse; the player picks the
    // best region itself when we get to streaming.
    size_t regions = r.body.find("\"regions\"");
    ep.host = Json_GetString(r.body, "baseUri", regions == std::string::npos ? 0 : regions);
    return ep;
}

// ============================================================================
// State
// ============================================================================

static std::mutex   s_mtx;
static std::atomic<bool> s_loginInFlight{false};
static std::atomic<bool> s_loginCancel{false};
static std::string  s_userCode;
static std::string  s_verifyUri;
static std::string  s_loginStatus;

static std::atomic<bool> s_sessionRunning{false};
static std::atomic<bool> s_sessionReady{false};
static std::string  s_sessionPhase;
static std::string  s_gamertag;
static StreamEndpoint s_xhome;
static StreamEndpoint s_xcloud;

static std::atomic<bool> s_consolesReady{false};
static std::atomic<bool> s_gamesReady{false};
static std::atomic<bool> s_consolesStarted{false};
static std::atomic<bool> s_gamesStarted{false};
static std::vector<XcloudConsole> s_consoles;
static std::vector<XcloudGame>    s_games;

static std::atomic<bool> s_streamRunning{false};
static std::atomic<bool> s_streamReady{false};
static std::atomic<bool> s_streamCancel{false};
static std::atomic<uint64_t> s_streamGen{0};   // bumps each new stream; UI resets on change
static std::string  s_streamPhase;
static std::string  s_streamPath;   // "/sessions/<type>/<id>"

static std::thread s_loginThread;
static std::thread s_sessionThread;
static std::thread s_consolesThread;
static std::thread s_gamesThread;
static std::thread s_streamThread;


static void SetPhase(const char* p) { std::lock_guard<std::mutex> l(s_mtx); s_sessionPhase = p; }

// ============================================================================
// Login (device code flow)
// ============================================================================

static void LoginWorker()
{
    HttpHeaders form = { { "Content-Type", "application/x-www-form-urlencoded" } };
    std::string body = std::string("client_id=") + kClientId +
        "&scope=xboxlive.signin%20openid%20profile%20offline_access";

    HttpResponse r = Http_Post(kDeviceCodeUrl, body, form);
    if (!r.ok()) {
        { std::lock_guard<std::mutex> l(s_mtx); s_loginStatus = "Couldn't reach Microsoft."; }
        s_loginInFlight = false;
        return;
    }

    std::string deviceCode = Json_GetString(r.body, "device_code");
    int interval = Json_GetInt(r.body, "interval");
    if (interval < 1) interval = 5;
    {
        std::lock_guard<std::mutex> l(s_mtx);
        s_userCode  = Json_GetString(r.body, "user_code");
        s_verifyUri = Json_GetString(r.body, "verification_uri");
        if (s_verifyUri.empty()) s_verifyUri = "https://microsoft.com/link";
        s_loginStatus = "Waiting for you to approve...";
    }

    std::string pollBody = std::string("grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=")
        + kClientId + "&device_code=" + deviceCode;

    for (;;) {
        if (s_loginCancel) { s_loginInFlight = false; return; }
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        if (s_loginCancel) { s_loginInFlight = false; return; }

        HttpResponse p = Http_Post(kTokenUrl, pollBody, form);
        if (p.ok()) {
            std::string rt = Json_GetString(p.body, "refresh_token");
            if (!rt.empty()) {
                strncpy(g_xcloudRefreshToken, rt.c_str(), sizeof(g_xcloudRefreshToken) - 1);
                g_xcloudRefreshToken[sizeof(g_xcloudRefreshToken) - 1] = 0;
                SaveDesktopSettings();
            }
            { std::lock_guard<std::mutex> l(s_mtx); s_loginStatus = "Signed in."; }
            s_loginInFlight = false;
            return;
        }
        // Non-2xx while pending is normal (authorization_pending / slow_down).
        std::string err = Json_GetString(p.body, "error");
        if (err == "slow_down") interval += 5;
        else if (!err.empty() && err != "authorization_pending") {
            { std::lock_guard<std::mutex> l(s_mtx); s_loginStatus = "Login failed or expired."; }
            s_loginInFlight = false;
            return;
        }
    }
}

// ============================================================================
// Session (run the chain so browse has a host + token)
// ============================================================================

static void SessionWorker()
{
    SetPhase("Refreshing sign in...");
    std::string ms = RefreshMsToken();
    if (ms.empty()) { SetPhase("Sign in expired."); s_sessionRunning = false; return; }

    SetPhase("Authenticating with Xbox Live...");
    std::string user = GetXblUserToken(ms);
    if (user.empty()) { SetPhase("Xbox Live auth failed."); s_sessionRunning = false; return; }

    std::string gtg;
    std::string gssv = GetGssvToken(user, gtg);
    if (gssv.empty()) { SetPhase("Streaming auth failed."); s_sessionRunning = false; return; }

    SetPhase("Getting streaming tokens...");
    StreamEndpoint home  = GetStreamEndpoint(gssv, "xhome");
    StreamEndpoint cloud = GetStreamEndpoint(gssv, "xgpuweb");
    if (!cloud.ok()) cloud = GetStreamEndpoint(gssv, "xgpuwebf2p");

    {
        std::lock_guard<std::mutex> l(s_mtx);
        s_xhome    = home;
        s_xcloud   = cloud;
        s_gamertag = gtg;
        s_sessionPhase = "Ready.";
    }
    s_sessionReady = true;
    s_sessionRunning = false;
}

// ============================================================================
// Browse
// ============================================================================

static void ConsolesWorker()
{
    StreamEndpoint ep;
    { std::lock_guard<std::mutex> l(s_mtx); ep = s_xhome; }
    std::vector<XcloudConsole> out;
    if (ep.ok()) {
        HttpHeaders h = {
            { "Authorization", std::string("Bearer ") + ep.token },
            { "Accept", "application/json" },
            { "x-gssv-client", "XboxComBrowser" },
        };
        HttpResponse r = Http_Get(ep.host + "/v6/servers/home", h);
        if (r.ok()) {
            size_t arr = r.body.find("\"results\"");
            if (arr != std::string::npos) arr = r.body.find('[', arr);
            for (const std::string& o : Json_SplitArray(r.body, arr == std::string::npos ? 0 : arr)) {
                XcloudConsole c;
                c.serverId    = Json_GetString(o, "serverId");
                c.name        = Json_GetString(o, "deviceName");
                c.powerState  = Json_GetString(o, "powerState");
                c.consoleType = Json_GetString(o, "consoleType");
                c.isDevKit    = o.find("\"isDevKit\":true") != std::string::npos;
                if (!c.serverId.empty()) out.push_back(c);
            }
        }
    }
    { std::lock_guard<std::mutex> l(s_mtx); s_consoles = out; }
    s_consolesReady = true;
}

static void GamesWorker()
{
    StreamEndpoint ep;
    { std::lock_guard<std::mutex> l(s_mtx); ep = s_xcloud; }
    std::vector<XcloudGame> out;
    if (ep.ok()) {
        HttpHeaders h = {
            { "Authorization", std::string("Bearer ") + ep.token },
            { "Accept", "application/json" },
            { "x-gssv-client", "XboxComBrowser" },
        };
        // Step 1: the titles list only carries slugs (titleId) and a productId.
        // The friendly name and art live in the store catalog, resolved below.
        HttpResponse r = Http_Get(ep.host + "/v2/titles", h);
        if (r.ok()) {
            size_t arr = r.body.find("\"results\"");
            if (arr != std::string::npos) arr = r.body.find('[', arr);
            for (const std::string& o : Json_SplitArray(r.body, arr == std::string::npos ? 0 : arr)) {
                XcloudGame g;
                g.titleId   = Json_GetString(o, "titleId");
                g.productId = Json_GetString(o, "productId");
                g.name      = g.titleId;   // placeholder until the catalog fills it in
                // Playable = owned (hasEntitlement), free to play, or granted by
                // a program the user actually holds for this title (userPrograms).
                // userSubscriptions is the user's tier list, present on every
                // title, so it is NOT a per title signal.
                g.playable  = Json_GetBool(o, "hasEntitlement") ||
                              Json_GetBool(o, "isFreeInStore") ||
                              Json_HasNonEmptyArray(o, "userPrograms");
                if (!g.titleId.empty() || !g.productId.empty()) out.push_back(g);
            }
        }

        // Step 2: resolve productId to name + tile art from the store catalog.
        std::string products;
        for (const XcloudGame& g : out) {
            if (g.productId.empty()) continue;
            if (!products.empty()) products += ",";
            products += "\"" + g.productId + "\"";
        }
        if (!products.empty()) {
            // Headers match the Xbox Cloud Gaming web client; the catalog
            // rejects unknown callers. It is a public catalog, no bearer.
            HttpHeaders ch = {
                { "Content-Type", "application/json" },
                { "Accept", "application/json" },
                { "ms-cv", "0" },
                { "calling-app-name", "Xbox Cloud Gaming Web" },
                { "calling-app-version", "21.0.0" },
            };
            std::string body = "{\"Products\":[" + products + "]}";
            HttpResponse cr = Http_Post(
                "https://catalog.gamepass.com/v3/products?market=US&language=en-US&hydration=RemoteHighSapphire0",
                body, ch);
            if (cr.ok()) {
                // Products comes back as an object keyed by id, not an array.
                size_t pobj = cr.body.find("\"Products\"");
                if (pobj != std::string::npos) pobj = cr.body.find('{', pobj);
                for (const std::string& p : Json_SplitObjectValues(cr.body, pobj == std::string::npos ? 0 : pobj)) {
                    std::string xid   = Json_GetString(p, "XCloudTitleId"); // == titleId slug
                    std::string sid   = Json_GetString(p, "StoreId");       // == productId
                    std::string title = Json_GetString(p, "ProductTitle");
                    std::string art;
                    size_t it = Json_FindKey(p, "Image_Tile");
                    if (it != std::string::npos) art = Json_GetString(p, "URL", it);
                    // Store image URLs come back protocol-relative (//host/...);
                    // curl needs a scheme.
                    if (art.rfind("//", 0) == 0) art = "https:" + art;
                    else if (!art.empty() && art.rfind("http", 0) != 0) art = "https://" + art;
                    for (XcloudGame& g : out) {
                        bool match = (!xid.empty() && g.titleId == xid) ||
                                     (!sid.empty() && g.productId == sid);
                        if (!match) continue;
                        if (!title.empty()) g.name = title;
                        if (!art.empty())   g.artUrl = art;
                    }
                }
            }
        }
    }
    { std::lock_guard<std::mutex> l(s_mtx); s_games = out; }
    s_gamesReady = true;
}

// ============================================================================
// Stream session (signaling half)
// ============================================================================

// The server wants to know who is asking. A minimal but well formed device
// descriptor is enough to provision; the real numbers do not matter to us.
static const char* kDeviceInfo =
    "{\"appInfo\":{\"env\":{\"clientAppId\":\"Theseus\",\"clientAppType\":\"native\","
    "\"clientAppVersion\":\"1.0.0\",\"clientSdkVersion\":\"8.5.3\",\"httpEnvironment\":\"prod\","
    "\"sdkInstallId\":\"\"}},\"dev\":{\"hw\":{\"make\":\"Apple\",\"model\":\"unknown\","
    "\"sdktype\":\"web\"},\"os\":{\"name\":\"macOS\",\"ver\":\"14\"},\"browser\":"
    "{\"browserName\":\"chrome\",\"browserVersion\":\"120.0\"},\"displayInfo\":"
    "{\"dimensions\":{\"widthInPixels\":1920,\"heightInPixels\":1080},"
    "\"pixelDensity\":{\"dpiX\":96,\"dpiY\":96}}}}";

static void SetStreamPhase(const std::string& p)
{
    std::lock_guard<std::mutex> l(s_mtx);
    s_streamPhase = p;
}

#if defined(THESEUS_HAVE_WEBRTC)

// Decode a Teredo IPv6 address (2001:0:...) to the tunneled IPv4 + UDP port.
// xCloud hands back only a Teredo candidate and no desktop OS speaks Teredo,
// so pull the real endpoint out and add it as a host candidate. IPv4 and port
// are stored ones complement. Mirrors xbox-xcloud-player's Teredo class.
// Input datachannel + handshake state, used by the render thread's
// Xcloud_SendGamepad. Set up in the transport once the peer is negotiated.
static std::shared_ptr<rtc::DataChannel> s_dcInput;
static std::shared_ptr<rtc::DataChannel> s_dcControl;
static std::mutex            s_chanMtx;
static std::atomic<bool>     s_inputReady{false};
static std::atomic<uint32_t> s_inputSeq{0};

// Kept for console remote play (xhome), where the console sits behind NAT and
// only offers a Teredo candidate. Unused for cloud, which has a public IPv4.
[[maybe_unused]] static bool TeredoDecode(const std::string& ipv6, std::string& ip4Out, int& portOut)
{
    std::vector<std::string> g;
    size_t start = 0;
    for (;;) {
        size_t c = ipv6.find(':', start);
        g.push_back(ipv6.substr(start, c == std::string::npos ? std::string::npos : c - start));
        if (c == std::string::npos) break;
        start = c + 1;
    }
    if (g.size() < 8) return false;               // needs the full, uncompressed form
    auto hx = [](const std::string& s){ return (unsigned)strtoul(s.c_str(), nullptr, 16); };
    unsigned ipObf   = (hx(g[6]) << 16) | hx(g[7]);
    unsigned port    = (~hx(g[5])) & 0xFFFF;
    unsigned ip      = ~ipObf;
    char buf[32];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    ip4Out = buf; portOut = (int)port;
    return true;
}

// Bring up the peer and get media flowing. Same order as the web player: make
// the offer, POST /sdp, take the answer, trade ICE, keep the session alive.
static void RunWebRtcTransport(const StreamEndpoint& ep, const std::string& sessionPath,
                               const HttpHeaders& hdr)
{
    rtc::InitLogger(rtc::LogLevel::Warning);
    XcloudVideo_Start();
    XcloudAudio_Start();
    rtc::Configuration config;
    // Need STUN or we only gather LAN candidates that can't reach xCloud.
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    auto pc = std::make_shared<rtc::PeerConnection>(config);

    std::mutex               cmtx;
    std::vector<std::string> localCands;
    std::atomic<bool>        gathered{false};
    std::atomic<int>         rtpCount{0};
    std::atomic<bool>        connected{false};
    std::atomic<bool>        peerDead{false};
    std::string              offerSdp;
    std::mutex               omtx;
    std::atomic<bool>        haveOffer{false};

    pc->onStateChange([&](rtc::PeerConnection::State s) {
        fprintf(stderr, "[xCloud] pc state=%d\n", (int)s);
        if (s == rtc::PeerConnection::State::Connected) connected = true;
        // Game exit, timeout, or a drop: the peer leaves Connected for good, so
        // we bail out and head back to the dashboard.
        if (s == rtc::PeerConnection::State::Disconnected ||
            s == rtc::PeerConnection::State::Failed ||
            s == rtc::PeerConnection::State::Closed)
            peerDead = true;
    });
    pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState s) {
        if (s == rtc::PeerConnection::GatheringState::Complete) gathered = true;
    });
    pc->onLocalDescription([&](rtc::Description d) {
        std::lock_guard<std::mutex> l(omtx);
        offerSdp = std::string(d);
        haveOffer = true;
    });
    pc->onLocalCandidate([&](rtc::Candidate c) {
        std::lock_guard<std::mutex> l(cmtx);
        localCands.push_back(std::string(c));
    });

    // Video (H264) and audio (Opus). Payload types and fmtp lines have to match
    // what xCloud answers with, lifted from green vita.
    rtc::Description::Video vid("video", rtc::Description::Direction::RecvOnly);
    vid.addH264Codec(102, "level-asymmetry-allowed=0;packetization-mode=1;"
                          "profile-level-id=42e020;max-fs=3600;max-mbps=108000");
    auto vtrack = pc->addTrack(vid);
    // Depacketize into Annex-B H264 and feed the decoder. The RtcpReceivingSession
    // handles receiver reports and lets us ask for a keyframe.
    auto depack      = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
    auto rtcpSession = std::make_shared<rtc::RtcpReceivingSession>();
    depack->addToChain(rtcpSession);
    vtrack->setMediaHandler(depack);
    vtrack->onFrame([&](rtc::binary data, rtc::FrameInfo) {
        rtpCount++;
        XcloudVideo_PushFrame(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    });

    rtc::Description::Audio aud("audio", rtc::Description::Direction::SendRecv);
    aud.addOpusCodec(111, "minptime=10;useinbandfec=1;stereo=1");
    auto atrack = pc->addTrack(aud);
    // One Opus frame per RTP packet, so the plain 48 kHz depacketizer just hands
    // us the payload.
    auto audioDepack  = std::make_shared<rtc::RtpDepacketizer>(48000);
    auto audioRtcp    = std::make_shared<rtc::RtcpReceivingSession>();
    audioDepack->addToChain(audioRtcp);
    atrack->setMediaHandler(audioDepack);
    atrack->onFrame([&](rtc::binary data, rtc::FrameInfo) {
        XcloudAudio_PushFrame(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    });

    // The channels the server expects. Input is unreliable so stale pad state
    // never blocks.
    rtc::DataChannelInit ci;
    ci = {}; ci.protocol = "chatV1";    auto dcChat    = pc->createDataChannel("chat", ci);
    ci = {}; ci.protocol = "controlV1"; auto dcControl = pc->createDataChannel("control", ci);
    ci = {}; ci.protocol = "messageV1"; auto dcMessage = pc->createDataChannel("message", ci);
    ci = {}; ci.protocol = "1.0"; ci.reliability.unordered = true; ci.reliability.maxRetransmits = 0u;
    auto dcInput = pc->createDataChannel("input", ci);

    { std::lock_guard<std::mutex> l(s_chanMtx); s_dcInput = dcInput; s_dcControl = dcControl; }

    // Handshake before input works: send Handshake on message, and on the ack
    // authorize control and announce a gamepad. No input is taken until then.
    dcMessage->onOpen([dcMessage]() {
        dcMessage->send(std::string(
            "{\"type\":\"Handshake\",\"version\":\"messageV1\","
            "\"id\":\"be0bfc6d-1e83-4c8a-90ed-fa8601c5a179\",\"cv\":\"0\"}"));
    });
    dcMessage->onMessage([dcControl](rtc::message_variant msg) {
        if (!std::holds_alternative<std::string>(msg)) return;
        if (std::get<std::string>(msg).find("HandshakeAck") == std::string::npos) return;
        if (dcControl && dcControl->isOpen()) {
            dcControl->send(std::string(
                "{\"message\":\"authorizationRequest\",\"accessKey\":\"4BDB3609-C1F1-4195-9B37-FEFF45DA8B8E\"}"));
            dcControl->send(std::string(
                "{\"message\":\"gamepadChanged\",\"gamepadIndex\":0,\"wasAdded\":true}"));
        }
        s_inputReady = true;
        fprintf(stderr, "[xCloud] input handshake complete\n");
    });

    pc->setLocalDescription();

    // Wait for the offer to be generated.
    for (int i = 0; i < 100 && !haveOffer && !s_streamCancel; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!haveOffer) { SetStreamPhase("Offer generation failed"); return; }

    std::string sdpBase = ep.host + sessionPath;

    // POST the offer, declaring the channel protocol versions xCloud expects.
    SetStreamPhase("Sending offer...");
    std::string offBody;
    { std::lock_guard<std::mutex> l(omtx);
      offBody = std::string("{\"messageType\":\"offer\",\"sdp\":") + JsonQuote(offerSdp) +
        ",\"requestId\":\"1\",\"configuration\":{"
        "\"chatConfiguration\":{\"bytesPerSample\":2,\"expectedClipDurationMs\":20,"
        "\"format\":{\"codec\":\"opus\",\"container\":\"webm\"},\"numChannels\":1,"
        "\"sampleFrequencyHz\":24000},"
        "\"chat\":{\"minVersion\":1,\"maxVersion\":1},"
        "\"control\":{\"minVersion\":1,\"maxVersion\":3},"
        "\"input\":{\"minVersion\":1,\"maxVersion\":9},"
        "\"message\":{\"minVersion\":1,\"maxVersion\":1},"
        "\"reliableinput\":{\"minVersion\":9,\"maxVersion\":9},"
        "\"unreliableinput\":{\"minVersion\":9,\"maxVersion\":9}}}"; }
    fprintf(stderr, "[xCloud] offer %zu bytes, POST /sdp...\n", offerSdp.size());
    HttpResponse pr = Http_Post(sdpBase + "/sdp", offBody, hdr);
    fprintf(stderr, "[xCloud] POST /sdp status=%ld body=%.300s\n", pr.status, pr.body.c_str());
    if (!pr.ok()) { SetStreamPhase("POST /sdp failed: HTTP " + std::to_string(pr.status)); return; }

    // Poll for the answer (204 until ready), then apply it.
    std::string answerSdp;
    for (int i = 0; i < 40 && answerSdp.empty() && !s_streamCancel; i++) {
        HttpResponse r = Http_Get(sdpBase + "/sdp", hdr);
        if (i == 0 || (r.status != 204 && r.status != 200))
            fprintf(stderr, "[xCloud] GET /sdp status=%ld body=%.300s\n", r.status, r.body.c_str());
        if (r.ok() && r.status != 204) {
            std::string ex = Json_GetString(r.body, "exchangeResponse");
            if (!ex.empty()) answerSdp = Json_GetString(ex, "sdp");
            if (answerSdp.empty())
                fprintf(stderr, "[xCloud] /sdp response had no exchangeResponse.sdp: %.300s\n", r.body.c_str());
        }
        if (answerSdp.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (answerSdp.empty()) { SetStreamPhase("No SDP answer"); return; }
    try {
        pc->setRemoteDescription(rtc::Description(answerSdp, rtc::Description::Type::Answer));
    } catch (const std::exception& e) {
        fprintf(stderr, "[xCloud] setRemoteDescription failed: %s\nSDP:\n%s\n", e.what(), answerSdp.c_str());
        SetStreamPhase("Bad SDP answer");
        return;
    }
    fprintf(stderr, "[xCloud] applied answer, %zu bytes\n", answerSdp.size());

    // Let ICE gathering finish, then hand our candidates over.
    for (int i = 0; i < 150 && !gathered && !s_streamCancel; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SetStreamPhase("Exchanging ICE...");
    // Our offer's ice-ufrag, echoed on every candidate as usernameFragment.
    std::string ufrag;
    { std::lock_guard<std::mutex> l(omtx);
      size_t u = offerSdp.find("ice-ufrag:");
      if (u != std::string::npos) { u += 10; size_t e = offerSdp.find_first_of("\r\n", u); ufrag = offerSdp.substr(u, e - u); } }

    // Each candidate is a stringified json object, not an object. No "a=" prefix,
    // mid is the bundle line ("video"), usernameFragment is our ufrag.
    std::string iceBody;
    { std::lock_guard<std::mutex> l(cmtx);
      iceBody = "{\"candidates\":[";
      for (size_t i = 0; i < localCands.size(); i++) {
          std::string c = localCands[i];
          if (c.rfind("a=", 0) == 0) c = c.substr(2);
          std::string inner = std::string("{\"candidate\":") + JsonQuote(c) +
                              ",\"sdpMid\":\"video\",\"sdpMLineIndex\":0,\"usernameFragment\":" +
                              JsonQuote(ufrag) + "}";
          if (i) iceBody += ",";
          iceBody += JsonQuote(inner);           // stringify the object
          fprintf(stderr, "[xCloud] local cand: %s\n", c.c_str());
      }
      iceBody += "]}";
    }
    fprintf(stderr, "[xCloud] posted %zu local candidates\n", localCands.size());
    Http_Post(sdpBase + "/ice", iceBody, hdr);

    // xCloud trickles its candidates in a few seconds after ours, and each GET
    // returns the full set, so we poll and dedup. Drop IPv6/Teredo (can't reach
    // them), keep IPv4. This loop also watches the peer and keeps the session alive.
    std::set<std::string> seen;
    int totalAdded = 0;
    auto lastKeepalive = std::chrono::steady_clock::now();
    for (int i = 0; !s_streamCancel && !peerDead; i++) {   // until stopped or the peer drops
        // Keep pulling candidates until the peer is up; once connected the
        // server has nothing more we need, so we stop hitting /ice.
        if (!connected) {
            HttpResponse r = Http_Get(sdpBase + "/ice", hdr);
            if (r.ok() && r.status != 204) {
                std::string ex = Json_GetString(r.body, "exchangeResponse");
                size_t arr = ex.find('[');
                for (const std::string& o : Json_SplitArray(ex, arr == std::string::npos ? 0 : arr)) {
                    std::string cand = Json_GetString(o, "candidate");
                    std::string mid  = Json_GetString(o, "sdpMid");
                    if (mid.empty()) mid = "0";
                    if (cand.rfind("a=", 0) == 0) cand = cand.substr(2);       // strip a=
                    if (cand.rfind("candidate:", 0) != 0) continue;
                    if (cand.find("end-of-candidates") != std::string::npos) continue;
                    // Field 4 is the address; skip it if it is IPv6 (has a colon).
                    std::vector<std::string> f; size_t st = 0;
                    while (st <= cand.size()) { size_t sp = cand.find(' ', st);
                        f.push_back(cand.substr(st, sp == std::string::npos ? std::string::npos : sp - st));
                        if (sp == std::string::npos) break; st = sp + 1; }
                    if (f.size() > 4 && f[4].find(':') != std::string::npos) continue;
                    if (!seen.insert(cand).second) continue;                   // dedup
                    fprintf(stderr, "[xCloud] remote cand: %s\n", cand.c_str());
                    try { pc->addRemoteCandidate(rtc::Candidate(cand, mid)); totalAdded++; } catch (...) {}
                }
            }
        }
        // Nudge the encoder for an IDR so the decoder can start. Retry for the
        // first few seconds until frames actually arrive.
        if (connected && rtpCount.load() < 2 && (i % 4) == 0) vtrack->requestKeyframe();

        if (connected) SetStreamPhase("Connected, RTP packets: " + std::to_string(rtpCount.load()));
        else           SetStreamPhase("Exchanging ICE... (" + std::to_string(totalAdded) + " server candidates)");
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastKeepalive).count() >= 20) {
            Http_Post(sdpBase + "/keepalive", "", hdr);
            lastKeepalive = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    fprintf(stderr, "[xCloud] transport exit, connected=%d rtp=%d remoteCands=%d\n",
            (int)connected, rtpCount.load(), totalAdded);
    s_inputReady = false;
    { std::lock_guard<std::mutex> l(s_chanMtx); s_dcInput.reset(); s_dcControl.reset(); }
    try { pc->close(); } catch (...) {}   // close ICE/DTLS/SCTP before we drop the peer
    XcloudVideo_Stop();
    XcloudAudio_Stop();
}

#endif // THESEUS_HAVE_WEBRTC

// Provision a play session and walk it to ReadyToConnect. type is "cloud" or
// "home"; id is the titleId (cloud) or serverId (home). Leaves the session up
// so the transport step can attach; teardown is Xcloud_StopStreamSession.
static void StreamSessionWorker(std::string type, std::string id)
{
    StreamEndpoint ep;
    { std::lock_guard<std::mutex> l(s_mtx); ep = (type == "home") ? s_xhome : s_xcloud; }
    if (!ep.ok()) { SetStreamPhase("No stream endpoint, sign in first"); s_streamRunning = false; return; }

    HttpHeaders h = {
        { "Authorization", std::string("Bearer ") + ep.token },
        { "Accept", "application/json" },
        { "Content-Type", "application/json" },
        { "X-Gssv-Client", "XboxComBrowser" },
        { "X-MS-Device-Info", kDeviceInfo },
    };

    SetStreamPhase("Requesting session...");
    std::string cloudTitle = (type == "cloud") ? id : std::string();
    std::string homeServer = (type == "home")  ? id : std::string();
    std::string playBody =
        std::string("{\"clientSessionId\":\"\",\"titleId\":\"") + cloudTitle + "\","
        "\"systemUpdateGroup\":\"\",\"settings\":{\"nanoVersion\":\"V3;WebrtcTransport.dll\","
        "\"enableOptionalDataCollection\":false,\"enableTextToSpeech\":false,\"highContrast\":0,"
        "\"locale\":\"en-US\",\"useIceConnection\":false,\"timezoneOffsetMinutes\":0,"
        "\"sdkType\":\"web\",\"osName\":\"windows\"},\"serverId\":\"" + homeServer + "\","
        "\"fallbackRegionNames\":[]}";

    HttpResponse pr = Http_Post(ep.host + "/v5/sessions/" + type + "/play", playBody, h);
    if (!pr.ok()) {
        std::string code = Json_GetString(pr.body, "code");
        std::string msg  = Json_GetString(pr.body, "message");
        SetStreamPhase(code.empty() ? ("play failed: HTTP " + std::to_string(pr.status))
                                    : (code + (msg.empty() ? "" : ": " + msg)));
        fprintf(stderr, "[xCloud] play HTTP %ld body=%.400s\n", pr.status, pr.body.c_str());
        s_streamRunning = false; return;
    }
    std::string sessionPath = Json_GetString(pr.body, "sessionPath");
    std::string sessionId   = Json_GetString(pr.body, "sessionId");
    if (sessionPath.empty()) { SetStreamPhase("play: no sessionPath"); s_streamRunning = false; return; }
    { std::lock_guard<std::mutex> l(s_mtx); s_streamPath = "/" + sessionPath; }
    fprintf(stderr, "[xCloud] session id=%s path=/%s\n", sessionId.c_str(), sessionPath.c_str());

    // Walk the session states: New, Provisioning, WaitingForResources,
    // ReadyToConnect, Provisioned. At ReadyToConnect we /connect with the passport
    // token, which pushes it to Provisioned. Only then does /sdp work.
    std::string statePath = ep.host + "/" + sessionPath + "/state";
    bool connectSent = false, provisioned = false;
    // Up to ~5 min: WaitingForResources is a real capacity queue, not a hang.
    for (int i = 0; i < 600 && !s_streamCancel; i++) {
        HttpResponse sr = Http_Get(statePath, h);
        std::string state = sr.ok() ? Json_GetString(sr.body, "state") : std::string();
        if (state == "WaitingForResources") SetStreamPhase("Waiting for a free cloud server...");
        else if (!state.empty())            SetStreamPhase("Session: " + state);

        if (state == "ReadyToConnect" && !connectSent) {
            SetStreamPhase("Authorizing...");
            std::string passport = GetPassportToken();
            if (passport.empty()) { SetStreamPhase("Passport token failed"); break; }
            HttpResponse cr = Http_Post(ep.host + "/" + sessionPath + "/connect",
                std::string("{\"userToken\":") + JsonQuote(passport) + "}", h);
            fprintf(stderr, "[xCloud] /connect status=%ld\n", cr.status);
            connectSent = true;
        }
        if (state == "Provisioned") { provisioned = true; s_streamReady = true; break; }
        if (state == "Error" || state == "Failed" || state == "Cancelled") { SetStreamPhase("Session " + state); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!provisioned && !s_streamCancel) SetStreamPhase("Session never provisioned");
    fprintf(stderr, "[xCloud] session worker: provisioned=%d\n", (int)provisioned);

#if defined(THESEUS_HAVE_WEBRTC)
    if (provisioned && !s_streamCancel)
        RunWebRtcTransport(ep, "/" + sessionPath, h);
#endif

    // Release the session so the cloud VM tears down now instead of idling to a
    // timeout. Covers stop, drop, and provisioned but never connected.
    if (!sessionPath.empty()) {
        HttpResponse dr = Http_Delete(ep.host + "/" + sessionPath, h);
        fprintf(stderr, "[xCloud] DELETE session status=%ld\n", dr.status);
    }
    s_streamRunning = false;
}

// ============================================================================
// Public API
// ============================================================================

void Xcloud_Init() {}

void Xcloud_Shutdown()
{
    s_loginCancel = true;
    s_streamCancel = true;
    JoinIf(s_loginThread);
    JoinIf(s_sessionThread);
    JoinIf(s_consolesThread);
    JoinIf(s_gamesThread);
    JoinIf(s_streamThread);
}

bool Xcloud_HasToken() { return g_xcloudRefreshToken[0] != 0; }

void Xcloud_SignOut()
{
    g_xcloudRefreshToken[0] = 0;
    s_sessionReady = false;
    s_consolesReady = false;
    s_gamesReady = false;
    s_consolesStarted = false;
    s_gamesStarted = false;
    { std::lock_guard<std::mutex> l(s_mtx); s_xhome = {}; s_xcloud = {}; s_gamertag.clear(); s_consoles.clear(); s_games.clear(); }
    SaveDesktopSettings();
}

void Xcloud_StartLogin()
{
    if (s_loginInFlight) return;
    JoinIf(s_loginThread);
    { std::lock_guard<std::mutex> l(s_mtx); s_userCode.clear(); s_verifyUri.clear(); s_loginStatus = "Requesting code..."; }
    s_loginCancel = false;
    s_loginInFlight = true;
    s_loginThread = std::thread(LoginWorker);
}

void Xcloud_CancelLogin() { s_loginCancel = true; }
bool Xcloud_LoginInFlight() { return s_loginInFlight; }

std::string Xcloud_GetUserCode()        { std::lock_guard<std::mutex> l(s_mtx); return s_userCode; }
std::string Xcloud_GetVerificationUri() { std::lock_guard<std::mutex> l(s_mtx); return s_verifyUri; }
std::string Xcloud_GetLoginStatus()     { std::lock_guard<std::mutex> l(s_mtx); return s_loginStatus; }

void Xcloud_StartSession()
{
    if (s_sessionReady || s_sessionRunning) return;
    if (!Xcloud_HasToken()) return;
    JoinIf(s_sessionThread);
    s_sessionRunning = true;
    s_sessionThread = std::thread(SessionWorker);
}

bool        Xcloud_SessionReady() { return s_sessionReady; }
std::string Xcloud_SessionPhase() { std::lock_guard<std::mutex> l(s_mtx); return s_sessionPhase; }
std::string Xcloud_GetGamertag()  { std::lock_guard<std::mutex> l(s_mtx); return s_gamertag; }

void Xcloud_FetchConsoles()
{
    if (!s_sessionReady) return;
    if (s_consolesStarted.exchange(true)) return;  // once per session
    s_consolesReady = false;
    s_consolesThread = std::thread(ConsolesWorker);
}

std::vector<XcloudConsole> Xcloud_GetConsoles() { std::lock_guard<std::mutex> l(s_mtx); return s_consoles; }
bool                       Xcloud_ConsolesReady() { return s_consolesReady; }

void Xcloud_FetchGames()
{
    if (!s_sessionReady) return;
    if (s_gamesStarted.exchange(true)) return;  // once per session
    s_gamesReady = false;
    s_gamesThread = std::thread(GamesWorker);
}

std::vector<XcloudGame> Xcloud_GetGames() { std::lock_guard<std::mutex> l(s_mtx); return s_games; }
bool                    Xcloud_GamesReady() { return s_gamesReady; }

// ============================================================================
// Box-art cache. Downloaded once to Library/Xcloud/<titleId>.jpg and read from
// disk thereafter; the launcher never re-pulls art it already has.
// ============================================================================

static std::mutex s_artMtx;
static std::map<std::string, std::atomic<bool>*> s_artInFlight;

// Host path curl writes to, next to the binary.
static std::string Xcloud_ArtCacheHostPath(const std::string& titleId)
{
    Mkdir_(AppPath_Tree("Library"));
    Mkdir_(AppPath_Tree("Library/Xcloud"));
    return std::string(AppPath_Tree("Library/Xcloud")) + "/" + titleId + ".jpg";
}

// Xbox-style path for the XAP; xboxfs maps E:\ onto Library/.
std::string Xcloud_ArtCachePath(const std::string& titleId)
{
    if (!Http_IsSafeFileId(titleId)) return "";
    return "E:\\Xcloud\\" + titleId + ".jpg";
}

static void ArtWorker(std::string titleId, std::string url, std::atomic<bool>* flag)
{
    std::string path = Xcloud_ArtCacheHostPath(titleId);
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        if (!Http_GetToFile(url, path))
            fprintf(stderr, "[xCloud] art fetch failed for %s (%s)\n", titleId.c_str(), url.c_str());
    }
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        s_artInFlight.erase(titleId);
    }
    delete flag;
}

void Xcloud_QueueArtDownload(const std::string& titleId, const std::string& url)
{
    // The id comes off the server and ends up in a file path.
    if (!Http_IsSafeFileId(titleId) || url.empty()) return;
    // Workers erase from the map on other threads, so it is only touched under the lock.
    std::atomic<bool>* flag;
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        if (s_artInFlight.count(titleId)) return;
        flag = new std::atomic<bool>(false);
        s_artInFlight[titleId] = flag;
    }
    std::thread(ArtWorker, titleId, url, flag).detach();
}

void Xcloud_StartStreamSession(const std::string& type, const std::string& id)
{
    if (s_streamRunning) return;
    if (!s_sessionReady) return;
    JoinIf(s_streamThread);
    s_streamReady = false;
    s_streamCancel = false;
    s_streamGen++;   // new stream: the fullscreen view resets its texture/buffer
#if defined(THESEUS_HAVE_WEBRTC)
    s_inputReady = false;
#endif
    { std::lock_guard<std::mutex> l(s_mtx); s_streamPhase = "Starting..."; s_streamPath.clear(); }
    s_streamRunning = true;
    s_streamThread = std::thread(StreamSessionWorker, type, id);
}

void Xcloud_StopStreamSession()
{
    s_streamCancel = true;
    // Transport teardown (peer close + session DELETE) runs in the worker.
}

void Xcloud_SendGamepad(const XcloudGamepad& gp)
{
#if defined(THESEUS_HAVE_WEBRTC)
    if (!s_inputReady) return;
    std::shared_ptr<rtc::DataChannel> ch;
    { std::lock_guard<std::mutex> l(s_chanMtx); ch = s_dcInput; }
    if (!ch || !ch->isOpen()) return;

    auto axis = [](float v) -> int16_t {
        float x = v * 32767.0f; if (x > 32767.0f) x = 32767.0f; if (x < -32767.0f) x = -32767.0f; return (int16_t)x; };
    auto trig = [](float v) -> uint16_t {
        if (v < 0) v = 0; float x = v * 65535.0f; if (x > 65535.0f) x = 65535.0f; return (uint16_t)x; };

    uint16_t mask = 0;
    if (gp.nexus) mask |= 2;    if (gp.menu)  mask |= 4;     if (gp.view)  mask |= 8;
    if (gp.a)     mask |= 16;   if (gp.b)     mask |= 32;    if (gp.x)     mask |= 64;   if (gp.y) mask |= 128;
    if (gp.up)    mask |= 256;  if (gp.down)  mask |= 512;   if (gp.left)  mask |= 1024; if (gp.right) mask |= 2048;
    if (gp.lb)    mask |= 4096; if (gp.rb)    mask |= 8192;  if (gp.ls)    mask |= 16384; if (gp.rs) mask |= 32768;

    std::vector<uint8_t> b;
    auto u16le = [&](uint16_t v){ b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); };
    auto i16le = [&](int16_t v){ u16le((uint16_t)v); };
    auto u32le = [&](uint32_t v){ b.push_back(v & 0xFF); b.push_back((v>>8)&0xFF); b.push_back((v>>16)&0xFF); b.push_back((v>>24)&0xFF); };
    auto u32be = [&](uint32_t v){ b.push_back((v>>24)&0xFF); b.push_back((v>>16)&0xFF); b.push_back((v>>8)&0xFF); b.push_back(v&0xFF); };

    u16le(2);                         // report type: Gamepad
    u32le(++s_inputSeq);              // sequence
    for (int i = 0; i < 8; i++) b.push_back(0);   // timestamp f64, ~0 at send time
    b.push_back(1);                   // one gamepad frame
    b.push_back(0);                   // gamepad index 0
    u16le(mask);
    i16le(axis(gp.lx));
    i16le(axis(-gp.ly));              // SDL Y is +down; xCloud wants +up
    i16le(axis(gp.rx));
    i16le(axis(-gp.ry));
    u16le(trig(gp.lt));
    u16le(trig(gp.rt));
    u32le(1);
    u32be(1);

    rtc::binary bin(b.size());
    for (size_t i = 0; i < b.size(); i++) bin[i] = (std::byte)b[i];
    try { ch->send(bin); } catch (...) {}
#else
    (void)gp;
#endif
}

bool        Xcloud_StreamRunning()    { return s_streamRunning; }
bool        Xcloud_StreamReady()      { return s_streamReady; }
uint64_t    Xcloud_StreamGeneration() { return s_streamGen.load(); }
std::string Xcloud_StreamPhase()      { std::lock_guard<std::mutex> l(s_mtx); return s_streamPhase; }
