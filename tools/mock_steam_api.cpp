// Mock der Steam-Flat-API fuer tools/check_steam.py.
//
// Bildet genau die Symbole nach, die GameBasiX dynamisch laedt: Identitaet,
// Freundesliste, P2P-Pakete, Erfolge, Statistiken, Ranglisten, Cloud-Speicher,
// Lobbys, Presence, Overlay, Session-Ticket und DLCs. So laesst sich der
// komplette Pfad ohne installiertes Steam pruefen. Keine C++-Standardbibliothek,
// damit die DLL ohne zusaetzliche Laufzeit-DLLs geladen werden kann.
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define GBX_EXPORT extern "C" __declspec(dllexport)
#else
#define GBX_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

constexpr unsigned long long kSelf = 76561190000000001ULL;
constexpr unsigned long long kFriend = 76561190000000002ULL;
constexpr unsigned kPacketSize = 1024;

// Jede Schnittstelle hat ihren eigenen Zeiger, und jede Funktion prueft ihn.
// So faellt auf, wenn GameBasiX den Accessor nur bindet statt ihn aufzurufen:
// dann kaeme eine Funktionsadresse als "this" an und alle Werte blieben leer.
constexpr std::uintptr_t kUserIface = 1;
constexpr std::uintptr_t kFriendsIface = 2;
constexpr std::uintptr_t kNetworkingIface = 3;
constexpr std::uintptr_t kUtilsIface = 4;
constexpr std::uintptr_t kStatsIface = 5;
constexpr std::uintptr_t kStorageIface = 6;
constexpr std::uintptr_t kMatchmakingIface = 7;
constexpr std::uintptr_t kAppsIface = 8;

// Kennungen, die der Mock zurueckgibt. Die Tests rechnen damit.
constexpr unsigned long long kBoard = 555;
constexpr unsigned long long kBoardEntries = 666;
constexpr unsigned long long kLobby = 90000000000000009ULL;
constexpr unsigned long long kCallFind = 1001;
constexpr unsigned long long kCallUpload = 1002;
constexpr unsigned long long kCallDownload = 1003;
constexpr unsigned long long kCallLobby = 1004;
constexpr int kWrongCall = 1999;

bool ifaceIs(void* self, std::uintptr_t expected) {
    return reinterpret_cast<std::uintptr_t>(self) == expected;
}

char g_packet[kPacketSize];
unsigned g_packetLength = 0;
int g_runCallbacks = 0;

// Erfolge und Statistiken
const char* const kAchievements[2] = {"FIRST_WIN", "BOSS_BEATEN"};
unsigned char g_unlocked[2] = {0, 0};
int g_kills = 0;
float g_playtime = 0.0f;
int g_stores = 0;
int g_score = 0;
int g_boards = 0;

// Cloud
constexpr int kMaxFiles = 8;
struct MockFile {
    char name[64];
    char data[4096];
    int size;
};
MockFile g_files[kMaxFiles];
int g_fileCount = 0;

// Lobby
unsigned long long g_joined = 0;
char g_lobbyValue[64] = "";

// Presence und Overlay
char g_presenceKey[64] = "";
char g_presenceValue[256] = "";
int g_overlays = 0;
char g_overlayPage[64] = "";

// Ticket
int g_ticketsCancelled = 0;

void storeFile(const char* name, const char* data, int size) {
    if (!name || !*name || size < 0 || size > static_cast<int>(sizeof(g_files[0].data))) return;
    for (int i = 0; i < g_fileCount; ++i) {
        if (std::strcmp(g_files[i].name, name) == 0) {
            std::memcpy(g_files[i].data, data, static_cast<size_t>(size));
            g_files[i].size = size;
            return;
        }
    }
    if (g_fileCount >= kMaxFiles) return;
    MockFile& file = g_files[g_fileCount];
    std::strncpy(file.name, name, sizeof(file.name) - 1);
    file.name[sizeof(file.name) - 1] = '\0';
    std::memcpy(file.data, data, static_cast<size_t>(size));
    file.size = size;
    ++g_fileCount;
}

int findFile(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < g_fileCount; ++i) {
        if (std::strcmp(g_files[i].name, name) == 0) return i;
    }
    return -1;
}

}  // namespace

GBX_EXPORT int SteamAPI_InitFlat(char* message) {
    if (message) message[0] = '\0';
    return 0;  // k_ESteamAPIInitResult_OK
}

GBX_EXPORT bool SteamAPI_InitSafe() { return true; }
GBX_EXPORT void SteamAPI_Shutdown() {}
GBX_EXPORT void SteamAPI_RunCallbacks() { ++g_runCallbacks; }

GBX_EXPORT void* SteamAPI_SteamUser_v023() { return reinterpret_cast<void*>(kUserIface); }
GBX_EXPORT void* SteamAPI_SteamFriends_v018() { return reinterpret_cast<void*>(kFriendsIface); }
GBX_EXPORT void* SteamAPI_SteamNetworking_v006() {
    return reinterpret_cast<void*>(kNetworkingIface);
}
GBX_EXPORT void* SteamAPI_SteamUtils_v011() { return reinterpret_cast<void*>(kUtilsIface); }
GBX_EXPORT void* SteamAPI_SteamUserStats_v013() { return reinterpret_cast<void*>(kStatsIface); }
GBX_EXPORT void* SteamAPI_SteamRemoteStorage_v016() {
    return reinterpret_cast<void*>(kStorageIface);
}
GBX_EXPORT void* SteamAPI_SteamMatchmaking_v009() {
    return reinterpret_cast<void*>(kMatchmakingIface);
}
GBX_EXPORT void* SteamAPI_SteamApps_v008() { return reinterpret_cast<void*>(kAppsIface); }

GBX_EXPORT unsigned long long SteamAPI_ISteamUser_GetSteamID(void* self) {
    return ifaceIs(self, kUserIface) ? kSelf : 0ULL;
}

GBX_EXPORT const char* SteamAPI_ISteamFriends_GetPersonaName(void* self) {
    return ifaceIs(self, kFriendsIface) ? "Mock Player" : "";
}

GBX_EXPORT int SteamAPI_ISteamFriends_GetFriendCount(void* self, int) {
    return ifaceIs(self, kFriendsIface) ? 1 : 0;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamFriends_GetFriendByIndex(void* self, int index,
                                                                     int) {
    if (!ifaceIs(self, kFriendsIface)) return 0ULL;
    return index == 0 ? kFriend : 0ULL;
}

GBX_EXPORT const char* SteamAPI_ISteamFriends_GetFriendPersonaName(void* self,
                                                                  unsigned long long id) {
    if (!ifaceIs(self, kFriendsIface)) return "";
    return id == kFriend ? "Mock Friend" : "Mock Player";
}

GBX_EXPORT int SteamAPI_ISteamFriends_GetFriendPersonaState(void* self, unsigned long long id) {
    if (!ifaceIs(self, kFriendsIface)) return 0;
    return id == kFriend ? 1 : 0;  // 1 = online
}

GBX_EXPORT uint32_t SteamAPI_ISteamUtils_GetAppID(void* self) {
    return ifaceIs(self, kUtilsIface) ? 480u : 0u;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_SendP2PPacket(void* self, unsigned long long to,
                                                        const void* data, uint32_t size,
                                                        int, int) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    if (to == 0 || size > kPacketSize) return false;
    std::memcpy(g_packet, data, size);
    g_packetLength = size;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_IsP2PPacketAvailable(void* self, uint32_t* size, int) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    if (g_packetLength == 0) {
        if (size) *size = 0;
        return false;
    }
    if (size) *size = g_packetLength;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_ReadP2PPacket(void* self, void* dest, uint32_t capacity,
                                                        uint32_t* got, unsigned long long* from,
                                                        int) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    if (g_packetLength == 0 || capacity < g_packetLength) return false;
    std::memcpy(dest, g_packet, g_packetLength);
    if (got) *got = g_packetLength;
    if (from) *from = kFriend;
    g_packetLength = 0;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser(void* self,
                                                                  unsigned long long user) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    return user != 0;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_CloseP2PSessionWithUser(void* self,
                                                                 unsigned long long user) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    return user != 0;
}

GBX_EXPORT bool SteamAPI_ISteamNetworking_GetP2PSessionState(void* self, unsigned long long user,
                                                             void* state) {
    if (!ifaceIs(self, kNetworkingIface)) return false;
    if (user == 0 || !state) return false;
    auto* bytes = static_cast<unsigned char*>(state);
    std::memset(bytes, 0, 18);
    bytes[0] = 1;  // m_bConnectionActive
    bytes[1] = 0;  // m_bConnecting
    bytes[3] = 0;  // m_bUsingRelay
    return true;
}

// ------------------------------------------------- Erfolge und Statistiken
GBX_EXPORT bool SteamAPI_ISteamUserStats_RequestCurrentStats(void* self) {
    return ifaceIs(self, kStatsIface);
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_GetAchievement(void* self, const char* name) {
    if (!ifaceIs(self, kStatsIface) || !name) return false;
    for (int i = 0; i < 2; ++i) {
        if (std::strcmp(name, kAchievements[i]) == 0) return g_unlocked[i] != 0;
    }
    return false;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_SetAchievement(void* self, const char* name) {
    if (!ifaceIs(self, kStatsIface) || !name) return false;
    for (int i = 0; i < 2; ++i) {
        if (std::strcmp(name, kAchievements[i]) == 0) {
            g_unlocked[i] = 1;
            return true;
        }
    }
    return false;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_StoreStats(void* self) {
    if (!ifaceIs(self, kStatsIface)) return false;
    ++g_stores;
    return true;
}

GBX_EXPORT uint32_t SteamAPI_ISteamUserStats_GetNumAchievements(void* self) {
    return ifaceIs(self, kStatsIface) ? 2u : 0u;
}

GBX_EXPORT const char* SteamAPI_ISteamUserStats_GetAchievementName(void* self, uint32_t index) {
    if (!ifaceIs(self, kStatsIface)) return "";
    return index < 2 ? kAchievements[index] : "";
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_GetStatInt32(void* self, const char* name,
                                                      int32_t* out) {
    if (!ifaceIs(self, kStatsIface) || !name || !out) return false;
    if (std::strcmp(name, "kills") != 0) return false;
    *out = g_kills;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_SetStatInt32(void* self, const char* name,
                                                      int32_t value) {
    if (!ifaceIs(self, kStatsIface) || !name || std::strcmp(name, "kills") != 0) return false;
    g_kills = value;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_GetStatFloat(void* self, const char* name, float* out) {
    if (!ifaceIs(self, kStatsIface) || !name || !out) return false;
    if (std::strcmp(name, "playtime") != 0) return false;
    *out = g_playtime;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_SetStatFloat(void* self, const char* name, float value) {
    if (!ifaceIs(self, kStatsIface) || !name || std::strcmp(name, "playtime") != 0) return false;
    g_playtime = value;
    return true;
}

// ------------------------------------------------------------ Ranglisten
GBX_EXPORT unsigned long long SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(
    void* self, const char* name, int, int) {
    if (!ifaceIs(self, kStatsIface) || !name || !*name) return 0ULL;
    ++g_boards;
    return kCallFind;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamUserStats_UploadLeaderboardScore(
    void* self, unsigned long long board, int, int32_t score, int32_t* details, int) {
    if (!ifaceIs(self, kStatsIface) || board != kBoard) return 0ULL;
    if (details) *details = 0;
    g_score = score;
    return kCallUpload;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(
    void* self, unsigned long long board, int, int, int, int) {
    if (!ifaceIs(self, kStatsIface) || board != kBoard) return 0ULL;
    return kCallDownload;
}

GBX_EXPORT bool SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(
    void* self, unsigned long long entries, int index, void* out, int32_t*, int) {
    if (!ifaceIs(self, kStatsIface) || entries != kBoardEntries || !out) return false;
    if (index < 0 || index > 1) return false;
    auto* bytes = static_cast<unsigned char*>(out);
    // unsigned long long user; int rank; int score; int details; unsigned long long ugc
    std::memset(bytes, 0, 32);
    const unsigned long long user = index == 0 ? kSelf : kFriend;
    std::memcpy(bytes, &user, 8);
    const int32_t rank = index == 0 ? 1 : 2;
    std::memcpy(bytes + 8, &rank, 4);
    const int32_t score = index == 0 ? g_score : 900;
    std::memcpy(bytes + 12, &score, 4);
    return true;
}

// ------------------------------------------------- asynchrone Ergebnisse
GBX_EXPORT bool SteamAPI_ISteamUtils_IsAPICallCompleted(void* self, unsigned long long call,
                                                        bool* failed) {
    if (!ifaceIs(self, kUtilsIface)) return false;
    if (failed) *failed = call == kWrongCall;
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamUtils_GetAPICallResult(void* self, unsigned long long call,
                                                      void* out, int size, int callback,
                                                      bool* failed) {
    if (!ifaceIs(self, kUtilsIface) || !out || size <= 0) return false;
    if (failed) *failed = false;
    auto* bytes = static_cast<unsigned char*>(out);
    std::memset(bytes, 0, static_cast<size_t>(size));
    if (callback == 1101 && call == kCallFind) {
        // LeaderboardFindResult_t
        const unsigned long long board = kBoard;
        if (size < 16) return false;
        std::memcpy(bytes, &board, 8);
        bytes[8] = 1;
        return true;
    }
    if (callback == 1104 && call == kCallUpload) {
        // LeaderboardScoreUploaded_t
        if (size < 20) return false;
        bytes[0] = 1;
        const int32_t score = g_score;
        std::memcpy(bytes + 4, &score, 4);
        bytes[8] = 1;
        const int32_t rank = 1;
        std::memcpy(bytes + 12, &rank, 4);
        const int32_t previous = 0;
        std::memcpy(bytes + 16, &previous, 4);
        return true;
    }
    if (callback == 1105 && call == kCallDownload) {
        // LeaderboardScoresDownloaded_t
        if (size < 24) return false;
        const unsigned long long board = kBoard;
        const unsigned long long entries = kBoardEntries;
        std::memcpy(bytes, &board, 8);
        std::memcpy(bytes + 8, &entries, 8);
        const int32_t count = 2;
        std::memcpy(bytes + 16, &count, 4);
        return true;
    }
    if (callback == 5131 && call == kCallLobby) {
        // LobbyCreated_t: int m_eResult; CSteamID m_ulSteamIDLobby
        if (size < 16) return false;
        const int32_t result = 1;
        std::memcpy(bytes, &result, 4);
        const unsigned long long lobby = kLobby;
        std::memcpy(bytes + 8, &lobby, 8);
        return true;
    }
    return false;
}

// ------------------------------------------------------ Steam-Cloud
GBX_EXPORT bool SteamAPI_ISteamRemoteStorage_FileWrite(void* self, const char* name,
                                                       const void* data, int32_t size) {
    if (!ifaceIs(self, kStorageIface) || !data) return false;
    storeFile(name, static_cast<const char*>(data), size);
    return true;
}

GBX_EXPORT int32_t SteamAPI_ISteamRemoteStorage_FileRead(void* self, const char* name, void* dest,
                                                        int32_t capacity) {
    if (!ifaceIs(self, kStorageIface) || !dest) return -1;
    const int index = findFile(name);
    if (index < 0) return -1;
    int size = g_files[index].size;
    if (capacity < size) size = capacity;
    std::memcpy(dest, g_files[index].data, static_cast<size_t>(size));
    return size;
}

GBX_EXPORT bool SteamAPI_ISteamRemoteStorage_FileExists(void* self, const char* name) {
    if (!ifaceIs(self, kStorageIface)) return false;
    return findFile(name) >= 0;
}

GBX_EXPORT bool SteamAPI_ISteamRemoteStorage_FileDelete(void* self, const char* name) {
    if (!ifaceIs(self, kStorageIface)) return false;
    const int index = findFile(name);
    if (index < 0) return false;
    for (int i = index; i + 1 < g_fileCount; ++i) g_files[i] = g_files[i + 1];
    --g_fileCount;
    return true;
}

GBX_EXPORT int32_t SteamAPI_ISteamRemoteStorage_GetFileCount(void* self) {
    return ifaceIs(self, kStorageIface) ? g_fileCount : 0;
}

GBX_EXPORT const char* SteamAPI_ISteamRemoteStorage_GetFileName(void* self, int32_t index,
                                                                int32_t* size) {
    if (!ifaceIs(self, kStorageIface) || index < 0 || index >= g_fileCount) return "";
    if (size) *size = g_files[index].size;
    return g_files[index].name;
}

// -------------------------------------------------------------- Lobbys
GBX_EXPORT unsigned long long SteamAPI_ISteamMatchmaking_CreateLobby(void* self, int, int) {
    if (!ifaceIs(self, kMatchmakingIface)) return 0ULL;
    return kCallLobby;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamMatchmaking_JoinLobby(void* self,
                                                                  unsigned long long id) {
    if (!ifaceIs(self, kMatchmakingIface) || id == 0) return 0ULL;
    g_joined = id;
    return 2000ULL;
}

GBX_EXPORT void SteamAPI_ISteamMatchmaking_LeaveLobby(void* self, unsigned long long) {
    if (ifaceIs(self, kMatchmakingIface)) g_joined = 0;
}

GBX_EXPORT int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void* self, unsigned long long id) {
    if (!ifaceIs(self, kMatchmakingIface)) return 0;
    if (id != 0 && (id == kLobby || id == g_joined)) return 2;
    return 0;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamMatchmaking_GetLobbyOwner(void* self,
                                                                      unsigned long long id) {
    if (!ifaceIs(self, kMatchmakingIface) || id == 0) return 0ULL;
    return kSelf;
}

GBX_EXPORT unsigned long long SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(
    void* self, unsigned long long id, int index) {
    if (!ifaceIs(self, kMatchmakingIface) || id == 0) return 0ULL;
    if (index == 0) return kSelf;
    if (index == 1) return kFriend;
    return 0ULL;
}

GBX_EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyData(void* self, unsigned long long id,
                                                        const char* key, const char* value) {
    if (!ifaceIs(self, kMatchmakingIface) || id == 0 || !key || !value) return false;
    if (std::strcmp(key, "level") != 0) return false;
    std::strncpy(g_lobbyValue, value, sizeof(g_lobbyValue) - 1);
    g_lobbyValue[sizeof(g_lobbyValue) - 1] = '\0';
    return true;
}

GBX_EXPORT const char* SteamAPI_ISteamMatchmaking_GetLobbyData(void* self, unsigned long long id,
                                                               const char* key) {
    if (!ifaceIs(self, kMatchmakingIface) || id == 0 || !key) return "";
    if (std::strcmp(key, "level") == 0) return g_lobbyValue;
    return "";
}

// ------------------------------------ Presence, Overlay, Ticket, DLC
GBX_EXPORT bool SteamAPI_ISteamFriends_SetRichPresence(void* self, const char* key,
                                                       const char* value) {
    if (!ifaceIs(self, kFriendsIface) || !key || !value) return false;
    std::strncpy(g_presenceKey, key, sizeof(g_presenceKey) - 1);
    g_presenceKey[sizeof(g_presenceKey) - 1] = '\0';
    std::strncpy(g_presenceValue, value, sizeof(g_presenceValue) - 1);
    g_presenceValue[sizeof(g_presenceValue) - 1] = '\0';
    return true;
}

GBX_EXPORT bool SteamAPI_ISteamFriends_GetRichPresence(void* self, const char* key, char* value,
                                                       int capacity) {
    if (!ifaceIs(self, kFriendsIface) || !key || !value || capacity <= 0) return false;
    if (std::strcmp(key, g_presenceKey) != 0) return false;
    std::strncpy(value, g_presenceValue, static_cast<size_t>(capacity) - 1);
    value[static_cast<size_t>(capacity) - 1] = '\0';
    return true;
}

GBX_EXPORT void SteamAPI_ISteamFriends_ActivateGameOverlay(void* self, const char* page) {
    if (!ifaceIs(self, kFriendsIface) || !page) return;
    ++g_overlays;
    std::strncpy(g_overlayPage, page, sizeof(g_overlayPage) - 1);
    g_overlayPage[sizeof(g_overlayPage) - 1] = '\0';
}

GBX_EXPORT bool SteamAPI_ISteamFriends_BIsOverlayEnabled(void* self) {
    return ifaceIs(self, kFriendsIface);
}

GBX_EXPORT uint32_t SteamAPI_ISteamUser_GetAuthSessionTicket(void* self, void* ticket,
                                                             int capacity, uint32_t* size) {
    if (!ifaceIs(self, kUserIface) || !ticket || !size || capacity < 4) return 0;
    auto* bytes = static_cast<unsigned char*>(ticket);
    bytes[0] = 0xde;
    bytes[1] = 0xad;
    bytes[2] = 0xbe;
    bytes[3] = 0xef;
    *size = 4;
    return 7;
}

GBX_EXPORT void SteamAPI_ISteamUser_CancelAuthTicket(void* self, uint32_t) {
    if (ifaceIs(self, kUserIface)) ++g_ticketsCancelled;
}

GBX_EXPORT int SteamAPI_ISteamApps_GetDLCCount(void* self) {
    return ifaceIs(self, kAppsIface) ? 1 : 0;
}

GBX_EXPORT bool SteamAPI_ISteamApps_BIsDLCInstalled(void* self, uint32_t appid) {
    return ifaceIs(self, kAppsIface) && appid == 481u;
}
