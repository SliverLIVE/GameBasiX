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

bool Steam::unlockAchievement(const std::string&) { return false; }
bool Steam::achievementUnlocked(const std::string&) { return false; }
int Steam::achievementCount() { return 0; }
std::string Steam::achievementName(int) { return ""; }
bool Steam::getStatInt(const std::string&, int& value) {
    value = 0;
    return false;
}
bool Steam::getStatFloat(const std::string&, float& value) {
    value = 0;
    return false;
}
bool Steam::setStatInt(const std::string&, int) { return false; }
bool Steam::setStatFloat(const std::string&, float) { return false; }
bool Steam::storeStats() { return false; }

bool Steam::openBoard(const std::string&, bool) { return false; }
bool Steam::uploadScore(int, bool) { return false; }
int Steam::downloadBoard(int, int) { return 0; }
std::string Steam::boardName(int) { return ""; }
std::string Steam::boardUser(int) { return ""; }
int Steam::boardScore(int) { return 0; }
int Steam::boardPlace(int) { return 0; }

bool Steam::cloudWrite(const std::string&, const std::string&) { return false; }
std::string Steam::cloudRead(const std::string&) { return ""; }
bool Steam::cloudExists(const std::string&) { return false; }
bool Steam::cloudDelete(const std::string&) { return false; }
int Steam::cloudCount() { return 0; }
std::string Steam::cloudName(int) { return ""; }
int Steam::cloudSize(const std::string&) { return 0; }

bool Steam::createLobby(int, int) { return false; }
bool Steam::joinLobby(unsigned long long) { return false; }
void Steam::leaveLobby() {}
bool Steam::inLobby() { return false; }
unsigned long long Steam::lobbyOwner() { return 0; }
int Steam::lobbyMembers() { return 0; }
unsigned long long Steam::lobbyMember(int) { return 0; }
std::string Steam::lobbyMemberName(int) { return ""; }
bool Steam::setLobbyData(const std::string&, const std::string&) { return false; }
std::string Steam::lobbyData(const std::string&) { return ""; }

bool Steam::setPresence(const std::string&, const std::string&) { return false; }
std::string Steam::presence(const std::string&) { return ""; }
bool Steam::clearPresence(const std::string&) { return false; }
void Steam::overlay(const std::string&) {}
bool Steam::overlayEnabled() { return false; }
std::string Steam::authTicket() { return ""; }
void Steam::cancelTicket() {}
int Steam::dlcCount() { return 0; }
bool Steam::dlcInstalled(int) { return false; }

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
                      versioned(fnUserIface_, "SteamAPI_SteamUser", 30) &&
                      versioned(fnFriendsIface_, "SteamAPI_SteamFriends", 30) &&
                      versioned(fnNetworkingIface_, "SteamAPI_SteamNetworking", 30) &&
                      symbol(fnGetSteamId_, "SteamAPI_ISteamUser_GetSteamID") &&
                      symbol(fnPersonaName_, "SteamAPI_ISteamFriends_GetPersonaName") &&
                      symbol(fnSendP2P_, "SteamAPI_ISteamNetworking_SendP2PPacket") &&
                      symbol(fnP2PAvailable_, "SteamAPI_ISteamNetworking_IsP2PPacketAvailable") &&
                      symbol(fnReadP2P_, "SteamAPI_ISteamNetworking_ReadP2PPacket") &&
                      symbol(fnAcceptP2P_, "SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser") &&
                      symbol(fnCloseP2P_, "SteamAPI_ISteamNetworking_CloseP2PSessionWithUser") &&
                      symbol(fnP2PState_, "SteamAPI_ISteamNetworking_GetP2PSessionState");
    if (!core) {
        err = "steam_api does not match the flat API (missing symbols)";
        return false;
    }
    // Optional: Freundesliste und App-ID; ohne sie bleiben die Befehle leer.
    symbol(fnRunCallbacks_, "SteamAPI_RunCallbacks");
    symbol(fnFriendCount_, "SteamAPI_ISteamFriends_GetFriendCount");
    symbol(fnFriendByIndex_, "SteamAPI_ISteamFriends_GetFriendByIndex");
    symbol(fnFriendName_, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    symbol(fnFriendState_, "SteamAPI_ISteamFriends_GetFriendPersonaState");
    versioned(fnUtilsIface_, "SteamAPI_SteamUtils", 12);
    symbol(fnAppId_, "SteamAPI_ISteamUtils_GetAppID");

    // Asynchrone Aufrufe abholen; ohne diese beiden bleiben Ranglisten leer.
    symbol(fnIsCallCompleted_, "SteamAPI_ISteamUtils_IsAPICallCompleted");
    symbol(fnGetCallResult_, "SteamAPI_ISteamUtils_GetAPICallResult");

    // Erfolge und Statistiken (optional).
    versioned(fnStatsIface_, "SteamAPI_SteamUserStats", 20);
    symbol(fnRequestStats_, "SteamAPI_ISteamUserStats_RequestCurrentStats");
    symbol(fnGetAchievement_, "SteamAPI_ISteamUserStats_GetAchievement");
    symbol(fnSetAchievement_, "SteamAPI_ISteamUserStats_SetAchievement");
    symbol(fnStoreStats_, "SteamAPI_ISteamUserStats_StoreStats");
    symbol(fnAchievementCount_, "SteamAPI_ISteamUserStats_GetNumAchievements");
    symbol(fnAchievementName_, "SteamAPI_ISteamUserStats_GetAchievementName");
    if (!symbol(fnGetStatInt_, "SteamAPI_ISteamUserStats_GetStatInt32"))
        symbol(fnGetStatInt_, "SteamAPI_ISteamUserStats_GetStat");
    symbol(fnGetStatFloat_, "SteamAPI_ISteamUserStats_GetStatFloat");
    symbol(fnSetStatInt_, "SteamAPI_ISteamUserStats_SetStatInt32");
    symbol(fnSetStatFloat_, "SteamAPI_ISteamUserStats_SetStatFloat");
    symbol(fnFindBoard_, "SteamAPI_ISteamUserStats_FindOrCreateLeaderboard");
    symbol(fnUploadScore_, "SteamAPI_ISteamUserStats_UploadLeaderboardScore");
    symbol(fnDownloadScores_, "SteamAPI_ISteamUserStats_DownloadLeaderboardEntries");
    symbol(fnGetBoardEntry_, "SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry");

    // Steam-Cloud (optional).
    versioned(fnStorageIface_, "SteamAPI_SteamRemoteStorage", 20);
    symbol(fnFileWrite_, "SteamAPI_ISteamRemoteStorage_FileWrite");
    symbol(fnFileRead_, "SteamAPI_ISteamRemoteStorage_FileRead");
    symbol(fnFileExists_, "SteamAPI_ISteamRemoteStorage_FileExists");
    symbol(fnFileDelete_, "SteamAPI_ISteamRemoteStorage_FileDelete");
    symbol(fnFileCount_, "SteamAPI_ISteamRemoteStorage_GetFileCount");
    symbol(fnFileName_, "SteamAPI_ISteamRemoteStorage_GetFileName");

    // Lobbys (optional).
    versioned(fnMatchmakingIface_, "SteamAPI_SteamMatchmaking", 20);
    symbol(fnCreateLobby_, "SteamAPI_ISteamMatchmaking_CreateLobby");
    symbol(fnJoinLobby_, "SteamAPI_ISteamMatchmaking_JoinLobby");
    symbol(fnLeaveLobby_, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    symbol(fnLobbyOwner_, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    symbol(fnLobbyMembers_, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    symbol(fnLobbyMember_, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    symbol(fnSetLobbyData_, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    symbol(fnGetLobbyData_, "SteamAPI_ISteamMatchmaking_GetLobbyData");

    // Presence, Overlay und Session-Ticket.
    symbol(fnSetPresence_, "SteamAPI_ISteamFriends_SetRichPresence");
    symbol(fnGetPresence_, "SteamAPI_ISteamFriends_GetRichPresence");
    symbol(fnActivateOverlay_, "SteamAPI_ISteamFriends_ActivateGameOverlay");
    symbol(fnOverlayEnabled_, "SteamAPI_ISteamFriends_BIsOverlayEnabled");
    symbol(fnGetTicket_, "SteamAPI_ISteamUser_GetAuthSessionTicket");
    symbol(fnCancelTicket_, "SteamAPI_ISteamUser_CancelAuthTicket");

    // DLCs (optional).
    versioned(fnAppsIface_, "SteamAPI_SteamApps", 20);
    symbol(fnDlcCount_, "SteamAPI_ISteamApps_GetDLCCount");
    symbol(fnDlcInstalled_, "SteamAPI_ISteamApps_BIsDLCInstalled");
    return true;
}

// Die Accessoren liefern erst nach SteamAPI_Init eine Schnittstelle; davor
// antworten sie mit nullptr. Deshalb hier und nicht in bindApi, sonst haetten
// alle Befehle eine Adresse als "this" und wuerden beim ersten Aufruf stuerzen.
void Steam::bindInterfaces() {
    user_ = fnUserIface_ ? fnUserIface_() : nullptr;
    friends_ = fnFriendsIface_ ? fnFriendsIface_() : nullptr;
    networking_ = fnNetworkingIface_ ? fnNetworkingIface_() : nullptr;
    utils_ = fnUtilsIface_ ? fnUtilsIface_() : nullptr;
    stats_ = fnStatsIface_ ? fnStatsIface_() : nullptr;
    storage_ = fnStorageIface_ ? fnStorageIface_() : nullptr;
    matchmaking_ = fnMatchmakingIface_ ? fnMatchmakingIface_() : nullptr;
    apps_ = fnAppsIface_ ? fnAppsIface_() : nullptr;
    // Ohne diesen Aufruf kennt Steam die Erfolge und Statistiken des Spiels
    // nicht und GetAchievement antwortet immer mit false.
    if (stats_ && fnRequestStats_) fnRequestStats_(stats_);
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
        err = "steam_api not found - put steam_api64.dll next to the EXE";
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
                             : "SteamAPI_Init failed - laeuft Steam und stimmt steam_appid.txt?";
        }
    } else if (fnInitSafe_) {
        ok = fnInitSafe_();
        if (!ok) err = "SteamAPI_InitSafe failed - laeuft Steam und stimmt steam_appid.txt?";
    } else {
        err = "steam_api kennt weder SteamAPI_InitFlat noch SteamAPI_InitSafe";
    }
    if (!ok) {
        error_ = err;
        return false;
    }
    ready_ = true;
    bindInterfaces();
    return true;
}

void Steam::shutdown() {
    cancelTicket();
    if (ready_ && fnShutdown_) fnShutdown_();
    ready_ = false;
    user_ = friends_ = networking_ = utils_ = nullptr;
    stats_ = storage_ = matchmaking_ = apps_ = nullptr;
    board_ = 0;
    boardRank_ = 0;
    entries_.clear();
    lobby_ = 0;
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

// --------------------------------------------------------------- Erfolge
namespace {

// Ergebnisstrukturen der asynchronen Aufrufe. Die Reihenfolge entspricht den
// Steamworks-Headern; mehr Felder braucht GameBasiX nicht.
struct LeaderboardFindResult {
    unsigned long long board;
    unsigned char found;
};

struct LeaderboardScoreUploaded {
    unsigned char success;
    int score;
    unsigned char changed;
    int rankNew;
    int rankPrevious;
};

struct LeaderboardScoresDownloaded {
    unsigned long long board;
    unsigned long long entries;
    int count;
};

struct LeaderboardEntry {
    unsigned long long user;
    int rank;
    int score;
    int details;
    unsigned long long ugc;
};

struct LobbyCreated {
    int result;
    unsigned long long lobby;
};

// Callback-IDs: k_iSteamUserStatsCallbacks ist 1100, k_iSteamMatchmakingCallbacks
// 5000. Die Nummern sind Teil der Steamworks-ABI und stabil.
constexpr int kLeaderboardFind = 1101;
constexpr int kLeaderboardUploaded = 1104;
constexpr int kLeaderboardDownloaded = 1105;
constexpr int kLobbyCreated = 5131;
constexpr int kCloudLimit = 1024 * 1024;

}  // namespace

// Die Aufrufe von FindOrCreateLeaderboard, UploadLeaderboardScore, Download-
// LeaderboardEntries und CreateLobby liefern nur ein Handle: das Ergebnis holt
// SteamAPI_IsAPICallCompleted ab. Ranglisten und Lobbys warten deshalb bis zu
// drei Sekunden, alles andere bleibt im Frame.
bool Steam::waitCall(unsigned long long call, void* out, int size, int callback) {
    if (!call || !utils_ || !fnIsCallCompleted_ || !fnGetCallResult_) return false;
    const Uint64 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 3000) {
        bool failed = false;
        if (fnIsCallCompleted_(utils_, call, &failed)) {
            if (failed) return false;
            bool ioFailure = false;
            return fnGetCallResult_(utils_, call, out, size, callback, &ioFailure) && !ioFailure;
        }
        SDL_Delay(1);
    }
    error_ = "Steam did not answer in time";
    return false;
}

bool Steam::unlockAchievement(const std::string& name) {
    if (!ready_ || !stats_ || !fnSetAchievement_) {
        error_ = "this Steam library has no achievements";
        return false;
    }
    if (!fnSetAchievement_(stats_, name.c_str())) {
        error_ = "Steam does not know the achievement " + name;
        return false;
    }
    // Wie im SDK: SetAchievement merkt es nur vor, StoreStats schickt es.
    storeStats();
    return true;
}

bool Steam::achievementUnlocked(const std::string& name) {
    if (!ready_ || !stats_ || !fnGetAchievement_) return false;
    return fnGetAchievement_(stats_, name.c_str());
}

int Steam::achievementCount() {
    if (!ready_ || !stats_ || !fnAchievementCount_) return 0;
    return static_cast<int>(fnAchievementCount_(stats_));
}

std::string Steam::achievementName(int index) {
    if (!ready_ || !stats_ || !fnAchievementName_ || index < 0) return "";
    const char* name = fnAchievementName_(stats_, static_cast<unsigned>(index));
    return name ? name : "";
}

bool Steam::getStatInt(const std::string& name, int& value) {
    value = 0;
    if (!ready_ || !stats_ || !fnGetStatInt_) return false;
    int found = 0;
    if (!fnGetStatInt_(stats_, name.c_str(), &found)) {
        error_ = "Steam does not know the statistic " + name;
        return false;
    }
    value = found;
    return true;
}

bool Steam::getStatFloat(const std::string& name, float& value) {
    value = 0.0f;
    if (!ready_ || !stats_ || !fnGetStatFloat_) return false;
    float found = 0.0f;
    if (!fnGetStatFloat_(stats_, name.c_str(), &found)) {
        error_ = "Steam does not know the statistic " + name;
        return false;
    }
    value = found;
    return true;
}

bool Steam::setStatInt(const std::string& name, int value) {
    if (!ready_ || !stats_ || !fnSetStatInt_) {
        error_ = "this Steam library has no statistics";
        return false;
    }
    if (!fnSetStatInt_(stats_, name.c_str(), value)) {
        error_ = "Steam does not know the statistic " + name;
        return false;
    }
    return true;
}

bool Steam::setStatFloat(const std::string& name, float value) {
    if (!ready_ || !stats_ || !fnSetStatFloat_) {
        error_ = "this Steam library has no statistics";
        return false;
    }
    if (!fnSetStatFloat_(stats_, name.c_str(), value)) {
        error_ = "Steam does not know the statistic " + name;
        return false;
    }
    return true;
}

bool Steam::storeStats() {
    if (!ready_ || !stats_ || !fnStoreStats_) return false;
    if (!fnStoreStats_(stats_)) {
        error_ = "Steam did not store the statistics";
        return false;
    }
    return true;
}

bool Steam::openBoard(const std::string& name, bool ascending) {
    board_ = 0;
    boardRank_ = 0;
    entries_.clear();
    if (!ready_ || !stats_ || !fnFindBoard_) {
        error_ = "this Steam library has no leaderboards";
        return false;
    }
    // ELeaderboardSortMethod: 1 aufsteigend, 2 absteigend; Anzeige 1 = Zahl.
    const unsigned long long call =
        fnFindBoard_(stats_, name.c_str(), ascending ? 1 : 2, 1);
    LeaderboardFindResult result{};
    if (!waitCall(call, &result, static_cast<int>(sizeof(result)), kLeaderboardFind) ||
        !result.found || !result.board) {
        error_ = "Steam could not open the leaderboard " + name;
        return false;
    }
    board_ = result.board;
    return true;
}

bool Steam::uploadScore(int score, bool keepBest) {
    boardRank_ = 0;
    if (!board_ || !stats_ || !fnUploadScore_) {
        error_ = "call STEAM_BOARD first";
        return false;
    }
    int details = 0;
    // ELeaderboardUploadScoreMethod: 1 behaelt den besten Wert, 2 schreibt immer.
    const unsigned long long call = fnUploadScore_(stats_, board_, keepBest ? 1 : 2, score,
                                                   &details, 0);
    LeaderboardScoreUploaded result{};
    if (!waitCall(call, &result, static_cast<int>(sizeof(result)), kLeaderboardUploaded) ||
        !result.success) {
        error_ = "Steam did not accept the score";
        return false;
    }
    boardRank_ = result.rankNew;
    return true;
}

int Steam::downloadBoard(int count, int kind) {
    entries_.clear();
    if (!board_ || !stats_ || !fnDownloadScores_ || !fnGetBoardEntry_) {
        error_ = "call STEAM_BOARD first";
        return 0;
    }
    // ELeaderboardDataRequest: 0 global, 1 um den Spieler, 2 Freunde.
    const int request = kind == 1 ? 2 : kind == 2 ? 1 : 0;
    const int range = count < 1 ? 10 : count;
    const int start = request == 1 ? -range / 2 : 0;
    const int end = request == 1 ? (range + 1) / 2 - 1 : range - 1;
    const unsigned long long call = fnDownloadScores_(stats_, board_, request, start, end, 0);
    LeaderboardScoresDownloaded result{};
    if (!waitCall(call, &result, static_cast<int>(sizeof(result)), kLeaderboardDownloaded) ||
        result.count <= 0) {
        return 0;
    }
    for (int i = 0; i < result.count; ++i) {
        LeaderboardEntry entry{};
        if (!fnGetBoardEntry_(stats_, result.entries, i, &entry, nullptr, 0)) continue;
        BoardEntry item;
        item.user = entry.user ? std::to_string(entry.user) : std::string();
        item.name = "";
        if (entry.user && friends_ && fnFriendName_) {
            const char* name = fnFriendName_(friends_, entry.user);
            if (name) item.name = name;
        }
        if (item.name.empty()) item.name = item.user;
        item.rank = entry.rank;
        item.score = entry.score;
        entries_.push_back(item);
    }
    return boardCount();
}

std::string Steam::boardName(int index) {
    if (index < 0 || index >= boardCount()) return "";
    return entries_[static_cast<size_t>(index)].name;
}

std::string Steam::boardUser(int index) {
    if (index < 0 || index >= boardCount()) return "";
    return entries_[static_cast<size_t>(index)].user;
}

int Steam::boardScore(int index) {
    if (index < 0 || index >= boardCount()) return 0;
    return entries_[static_cast<size_t>(index)].score;
}

int Steam::boardPlace(int index) {
    if (index < 0 || index >= boardCount()) return 0;
    return entries_[static_cast<size_t>(index)].rank;
}

// ------------------------------------------------------------ Steam-Cloud
bool Steam::cloudWrite(const std::string& file, const std::string& data) {
    if (!ready_ || !storage_ || !fnFileWrite_) {
        error_ = "this Steam library has no cloud storage";
        return false;
    }
    if (!fnFileWrite_(storage_, file.c_str(), data.data(), static_cast<int>(data.size()))) {
        error_ = "Steam did not store " + file;
        return false;
    }
    return true;
}

int Steam::cloudSize(const std::string& file) {
    if (!ready_ || !storage_ || !fnFileCount_ || !fnFileName_) return 0;
    const int count = fnFileCount_(storage_);
    for (int i = 0; i < count; ++i) {
        int size = 0;
        const char* name = fnFileName_(storage_, i, &size);
        if (name && file == name) return size;
    }
    return 0;
}

std::string Steam::cloudRead(const std::string& file) {
    if (!ready_ || !storage_ || !fnFileRead_) return "";
    int size = cloudSize(file);
    if (size <= 0) size = 64 * 1024;
    if (size > kCloudLimit) size = kCloudLimit;
    std::vector<char> buffer(static_cast<size_t>(size) + 1);
    const int got = fnFileRead_(storage_, file.c_str(), buffer.data(), size);
    if (got <= 0) {
        if (got < 0) error_ = "Steam could not read " + file;
        return "";
    }
    return std::string(buffer.data(), static_cast<size_t>(got));
}

bool Steam::cloudExists(const std::string& file) {
    if (!ready_ || !storage_ || !fnFileExists_) return false;
    return fnFileExists_(storage_, file.c_str());
}

bool Steam::cloudDelete(const std::string& file) {
    if (!ready_ || !storage_ || !fnFileDelete_) return false;
    if (!fnFileDelete_(storage_, file.c_str())) {
        error_ = "Steam could not delete " + file;
        return false;
    }
    return true;
}

int Steam::cloudCount() {
    if (!ready_ || !storage_ || !fnFileCount_) return 0;
    return fnFileCount_(storage_);
}

std::string Steam::cloudName(int index) {
    if (!ready_ || !storage_ || !fnFileName_ || index < 0 || index >= cloudCount()) return "";
    const char* name = fnFileName_(storage_, index, nullptr);
    return name ? name : "";
}

// ---------------------------------------------------------------- Lobbys
bool Steam::createLobby(int slots, int type) {
    if (!ready_ || !matchmaking_ || !fnCreateLobby_) {
        error_ = "this Steam library has no lobbies";
        return false;
    }
    const int size = slots < 1 ? 4 : slots > 250 ? 250 : slots;
    const int lobbyType = type < 0 || type > 3 ? 2 : type;
    const unsigned long long call = fnCreateLobby_(matchmaking_, lobbyType, size);
    LobbyCreated result{};
    if (!waitCall(call, &result, static_cast<int>(sizeof(result)), kLobbyCreated) ||
        result.result != 1 || !result.lobby) {
        error_ = "Steam could not create the lobby";
        return false;
    }
    lobby_ = result.lobby;
    return true;
}

bool Steam::joinLobby(unsigned long long id) {
    if (!ready_ || !matchmaking_ || !fnJoinLobby_ || id == 0) {
        error_ = "call STEAM_INIT first and pass a lobby id";
        return false;
    }
    lobby_ = id;
    fnJoinLobby_(matchmaking_, id);
    // JoinLobby meldet den Erfolg per Callback; ohne Callback-Verwaltung fragt
    // GameBasiX stattdessen die Mitgliederliste ab.
    const Uint64 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 3000) {
        if (inLobby()) return true;
        SDL_Delay(1);
    }
    error_ = "Steam did not join the lobby";
    lobby_ = 0;
    return false;
}

void Steam::leaveLobby() {
    if (ready_ && matchmaking_ && fnLeaveLobby_ && lobby_) fnLeaveLobby_(matchmaking_, lobby_);
    lobby_ = 0;
}

bool Steam::inLobby() {
    if (!ready_ || !matchmaking_ || !fnLobbyMembers_ || !lobby_) return false;
    return fnLobbyMembers_(matchmaking_, lobby_) > 0;
}

unsigned long long Steam::lobbyOwner() {
    if (!inLobby() || !fnLobbyOwner_) return 0;
    return fnLobbyOwner_(matchmaking_, lobby_);
}

int Steam::lobbyMembers() {
    if (!inLobby()) return 0;
    return fnLobbyMembers_(matchmaking_, lobby_);
}

unsigned long long Steam::lobbyMember(int index) {
    if (!inLobby() || !fnLobbyMember_ || index < 0 || index >= lobbyMembers()) return 0;
    return fnLobbyMember_(matchmaking_, lobby_, index);
}

std::string Steam::lobbyMemberName(int index) {
    if (!friends_ || !fnFriendName_) return "";
    const unsigned long long id = lobbyMember(index);
    if (!id) return "";
    const char* name = fnFriendName_(friends_, id);
    return name ? name : "";
}

bool Steam::setLobbyData(const std::string& key, const std::string& value) {
    if (!inLobby() || !fnSetLobbyData_) {
        error_ = "join a lobby before setting its data";
        return false;
    }
    if (!fnSetLobbyData_(matchmaking_, lobby_, key.c_str(), value.c_str())) {
        error_ = "only the lobby owner may set its data";
        return false;
    }
    return true;
}

std::string Steam::lobbyData(const std::string& key) {
    if (!inLobby() || !fnGetLobbyData_) return "";
    const char* value = fnGetLobbyData_(matchmaking_, lobby_, key.c_str());
    return value ? value : "";
}

// ------------------------------------------------ Presence und Overlay
bool Steam::setPresence(const std::string& key, const std::string& value) {
    if (!ready_ || !friends_ || !fnSetPresence_) return false;
    if (!fnSetPresence_(friends_, key.c_str(), value.c_str())) {
        error_ = "Steam did not accept the presence " + key;
        return false;
    }
    return true;
}

std::string Steam::presence(const std::string& key) {
    if (!ready_ || !friends_ || !fnGetPresence_) return "";
    char buffer[256] = {0};
    if (!fnGetPresence_(friends_, key.c_str(), buffer, static_cast<int>(sizeof(buffer))))
        return "";
    return buffer;
}

bool Steam::clearPresence(const std::string& key) { return setPresence(key, ""); }

void Steam::overlay(const std::string& page) {
    if (!ready_ || !friends_ || !fnActivateOverlay_) return;
    const char* dialog = page.empty() ? "friends" : page.c_str();
    fnActivateOverlay_(friends_, dialog);
}

bool Steam::overlayEnabled() {
    if (!ready_ || !friends_ || !fnOverlayEnabled_) return false;
    return fnOverlayEnabled_(friends_);
}

// ------------------------------------------------------- Ticket und DLC
std::string Steam::authTicket() {
    if (!ready_ || !user_ || !fnGetTicket_) {
        error_ = "Steam is not ready";
        return "";
    }
    if (ticket_ && fnCancelTicket_) {
        fnCancelTicket_(user_, ticket_);
        ticket_ = 0;
    }
    unsigned char raw[1024] = {0};
    unsigned size = 0;
    ticket_ = fnGetTicket_(user_, raw, static_cast<int>(sizeof(raw)), &size);
    if (!ticket_ || size == 0 || size > sizeof(raw)) {
        ticket_ = 0;
        error_ = "Steam did not issue a ticket";
        return "";
    }
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (unsigned i = 0; i < size; ++i) {
        hex.push_back(digits[raw[i] >> 4]);
        hex.push_back(digits[raw[i] & 0x0f]);
    }
    return hex;
}

void Steam::cancelTicket() {
    if (ticket_ && ready_ && user_ && fnCancelTicket_) fnCancelTicket_(user_, ticket_);
    ticket_ = 0;
}

int Steam::dlcCount() {
    if (!ready_ || !apps_ || !fnDlcCount_) return 0;
    return fnDlcCount_(apps_);
}

bool Steam::dlcInstalled(int appid) {
    if (!ready_ || !apps_ || !fnDlcInstalled_ || appid <= 0) return false;
    return fnDlcInstalled_(apps_, static_cast<unsigned>(appid));
}

#endif  // __EMSCRIPTEN__
