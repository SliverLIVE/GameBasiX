// GameBasiX - Steamworks per Flat-API, ohne SDK beim Bauen
#include "steam.h"

// Ohne Desktop gibt es kein Steam: die Befehle bleiben leer, damit der
// HTML5-Player weiterhin baut.
#if defined(__EMSCRIPTEN__)

namespace {
const char* const kNoSteam = "Steam gibt es nur im Desktop-Build";
}

Steam::~Steam() {}
bool Steam::load(std::string& err) {
    err = kNoSteam;
    return false;
}
bool Steam::startup(std::uint32_t, std::string& err) {
    err = kNoSteam;
    return false;
}
void Steam::shutdown() {}
void Steam::runCallbacks() {}
unsigned long long Steam::userId() { return 0; }
std::string Steam::userName() { return ""; }
int Steam::appId() { return 0; }
int Steam::friendCount() { return 0; }
unsigned long long Steam::friendId(int) { return 0; }
std::string Steam::friendName(int) { return ""; }
int Steam::friendState(int) { return 0; }
int Steam::sendPacket(unsigned long long, const std::string&, int, bool) { return 0; }
int Steam::available(int) { return 0; }
std::string Steam::readPacket(int, unsigned long long& from) {
    from = 0;
    return "";
}
bool Steam::acceptSession(unsigned long long) { return false; }
bool Steam::closeSession(unsigned long long) { return false; }
int Steam::sessionState(unsigned long long) { return 0; }

#else

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace {

// Ausschnitt aus P2PSessionState_t; die Felder liegen in derselben Reihenfolge
// wie im SDK, damit Steam die Struktur direkt beschreiben kann.
#pragma pack(push, 1)
struct P2PSessionState {
    std::uint8_t connectionActive;
    std::uint8_t connecting;
    std::uint8_t sessionError;
    std::uint8_t usingRelay;
    std::int32_t bytesQueuedForSend;
    std::int32_t packetsQueuedForSend;
    std::uint32_t remoteIp;
    std::uint16_t remotePort;
};
#pragma pack(pop)

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string basePath() {
    // SDL 3 gibt hier einen internen Puffer zurueck: nicht freigeben.
    const char* raw = SDL_GetBasePath();
    return raw ? std::string(raw) : std::string();
}

}  // namespace

Steam::~Steam() {
    shutdown();
    if (lib_) SDL_UnloadObject(lib_);
}

template <typename T>
bool Steam::symbol(T& target, const char* name) {
    if (!lib_) return false;
    SDL_FunctionPointer raw = SDL_LoadFunction(lib_, name);
    if (!raw) return false;
    target = reinterpret_cast<T>(raw);
    return true;
}

// Die Accessoren heissen SteamAPI_SteamUser_v0XX usw.; die Versionsnummer
// haengt am SDK-Stand. Deshalb von oben nach unten durchprobieren.
template <typename T>
bool Steam::versioned(T& target, const char* base, int highest) {
    char name[96];
    for (int version = highest; version >= 1; --version) {
        std::snprintf(name, sizeof(name), "%s_v%03d", base, version);
        if (symbol(target, name)) return true;
    }
    return false;
}

bool Steam::bindApi(std::string& err) {
    // SteamAPI_Init ist im SDK inline; die DLL exportiert SteamAPI_InitFlat.
    // Aeltere Bibliotheken haben SteamAPI_InitSafe, deshalb beide versuchen.
    const bool hasInit = symbol(fnInitFlat_, "SteamAPI_InitFlat") ||
                         symbol(fnInitSafe_, "SteamAPI_InitSafe");
    const bool core = hasInit &&
                      symbol(fnShutdown_, "SteamAPI_Shutdown") &&
                      versioned(user_, "SteamAPI_SteamUser", 30) &&
                      versioned(friends_, "SteamAPI_SteamFriends", 30) &&
                      versioned(networking_, "SteamAPI_SteamNetworking", 30) &&
                      symbol(fnGetSteamId_, "SteamAPI_ISteamUser_GetSteamID") &&
                      symbol(fnPersonaName_, "SteamAPI_ISteamFriends_GetPersonaName") &&
                      symbol(fnSendP2P_, "SteamAPI_ISteamNetworking_SendP2PPacket") &&
                      symbol(fnP2PAvailable_, "SteamAPI_ISteamNetworking_IsP2PPacketAvailable") &&
                      symbol(fnReadP2P_, "SteamAPI_ISteamNetworking_ReadP2PPacket") &&
                      symbol(fnAcceptP2P_, "SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser") &&
                      symbol(fnCloseP2P_, "SteamAPI_ISteamNetworking_CloseP2PSessionWithUser") &&
                      symbol(fnP2PState_, "SteamAPI_ISteamNetworking_GetP2PSessionState");
    if (!core) {
        err = "steam_api passt nicht zur Flat-API (fehlende Symbole)";
        return false;
    }
    // Optional: Freundesliste und App-ID; ohne sie bleiben die Befehle leer.
    symbol(fnRunCallbacks_, "SteamAPI_RunCallbacks");
    symbol(fnFriendCount_, "SteamAPI_ISteamFriends_GetFriendCount");
    symbol(fnFriendByIndex_, "SteamAPI_ISteamFriends_GetFriendByIndex");
    symbol(fnFriendName_, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    symbol(fnFriendState_, "SteamAPI_ISteamFriends_GetFriendPersonaState");
    if (versioned(utils_, "SteamAPI_SteamUtils", 12))
        symbol(fnAppId_, "SteamAPI_ISteamUtils_GetAppID");
    return true;
}

bool Steam::load(std::string& err) {
    if (lib_) return true;
    std::vector<std::string> names;
    if (const char* override = std::getenv("GBX_STEAM_LIB"); override && *override)
        names.emplace_back(override);
    const std::string base = basePath();
#if defined(_WIN32)
    if (!base.empty()) {
        names.push_back(base + "steam_api64.dll");
        names.push_back(base + "steam_api.dll");
    }
    names.emplace_back("steam_api64.dll");
    names.emplace_back("steam_api.dll");
#elif defined(__APPLE__)
    names.emplace_back("libsteam_api.dylib");
    if (!base.empty()) names.push_back(base + "libsteam_api.dylib");
#else
    names.emplace_back("libsteam_api.so");
    if (!base.empty()) names.push_back(base + "libsteam_api.so");
#endif
    for (const std::string& name : names) {
        lib_ = SDL_LoadObject(name.c_str());
        if (lib_) break;
    }
    if (!lib_) {
        err = "steam_api nicht gefunden - steam_api64.dll neben die EXE legen";
        error_ = err;
        return false;
    }
    if (!bindApi(err)) {
        SDL_UnloadObject(lib_);
        lib_ = nullptr;
        error_ = err;
        return false;
    }
    return true;
}

bool Steam::startup(std::uint32_t appId, std::string& err) {
    if (ready_) return true;
    if (!load(err)) return false;
    if (appId != 0) {
        const std::string value = std::to_string(appId);
        setEnvironment("SteamAppId", value.c_str());
    }
    bool ok = false;
    if (fnInitFlat_) {
        char message[1024] = {0};
        ok = fnInitFlat_(message) == 0;
        if (!ok) {
            err = message[0] ? std::string(message)
                             : "SteamAPI_Init fehlgeschlagen - laeuft Steam und stimmt steam_appid.txt?";
        }
    } else if (fnInitSafe_) {
        ok = fnInitSafe_();
        if (!ok) err = "SteamAPI_InitSafe fehlgeschlagen - laeuft Steam und stimmt steam_appid.txt?";
    } else {
        err = "steam_api kennt weder SteamAPI_InitFlat noch SteamAPI_InitSafe";
    }
    if (!ok) {
        error_ = err;
        return false;
    }
    ready_ = true;
    return true;
}

void Steam::shutdown() {
    if (ready_ && fnShutdown_) fnShutdown_();
    ready_ = false;
    user_ = friends_ = networking_ = utils_ = nullptr;
}

bool Steam::hasSession() { return ready_ && networking_; }

void Steam::runCallbacks() {
    if (ready_ && fnRunCallbacks_) fnRunCallbacks_();
}

unsigned long long Steam::userId() {
    if (!ready_ || !user_ || !fnGetSteamId_) return 0;
    return fnGetSteamId_(user_);
}

std::string Steam::userName() {
    if (!ready_ || !friends_ || !fnPersonaName_) return "";
    const char* name = fnPersonaName_(friends_);
    return name ? name : "";
}

int Steam::appId() {
    if (!ready_ || !utils_ || !fnAppId_) return 0;
    return static_cast<int>(fnAppId_(utils_));
}

int Steam::friendCount() {
    if (!ready_ || !friends_ || !fnFriendCount_) return 0;
    return fnFriendCount_(friends_, 4 /* k_EFriendFlagImmediate */);
}

unsigned long long Steam::friendId(int index) {
    if (!ready_ || !friends_ || !fnFriendByIndex_) return 0;
    return fnFriendByIndex_(friends_, index, 4);
}

std::string Steam::friendName(int index) {
    if (!ready_ || !friends_ || !fnFriendName_) return "";
    const unsigned long long id = friendId(index);
    if (!id) return "";
    const char* name = fnFriendName_(friends_, id);
    return name ? name : "";
}

int Steam::friendState(int index) {
    if (!ready_ || !friends_ || !fnFriendState_) return 0;
    const unsigned long long id = friendId(index);
    if (!id) return 0;
    return fnFriendState_(friends_, id);
}

int Steam::sendPacket(unsigned long long to, const std::string& data, int channel, bool reliable) {
    if (!hasSession() || to == 0) return 0;
    // eP2PSendType: 0 unreliable, 1 unreliable no nagle, 2 reliable, 3 reliable no nagle.
    const int type = reliable ? 2 : 0;
    if (!fnSendP2P_(networking_, to, data.data(), static_cast<unsigned>(data.size()), type, channel))
        return 0;
    return static_cast<int>(data.size());
}

int Steam::available(int channel) {
    if (!hasSession()) return 0;
    unsigned size = 0;
    if (!fnP2PAvailable_(networking_, &size, channel)) return 0;
    return static_cast<int>(size);
}

std::string Steam::readPacket(int channel, unsigned long long& from) {
    from = 0;
    if (!hasSession()) return "";
    unsigned size = 0;
    if (!fnP2PAvailable_(networking_, &size, channel) || size == 0) return "";
    std::vector<char> buffer(size + 1);
    unsigned got = 0;
    if (!fnReadP2P_(networking_, buffer.data(), size, &got, &from, channel)) return "";
    lastFrom_ = from;
    return std::string(buffer.data(), got);
}

bool Steam::acceptSession(unsigned long long user) {
    if (!hasSession() || user == 0) return false;
    return fnAcceptP2P_(networking_, user);
}

bool Steam::closeSession(unsigned long long user) {
    if (!hasSession() || user == 0) return false;
    return fnCloseP2P_(networking_, user);
}

int Steam::sessionState(unsigned long long user) {
    if (!hasSession() || user == 0) return 0;
    P2PSessionState state;
    std::memset(&state, 0, sizeof(state));
    if (!fnP2PState_(networking_, user, &state)) return 0;
    if (state.connectionActive) return state.usingRelay ? 3 : 2;
    if (state.connecting) return 1;
    return 0;
}

#endif  // __EMSCRIPTEN__
