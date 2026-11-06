// GameBasiX - Steamworks ueber die stabile C-Flat-API
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_SharedObject;

// Die Steam-Bibliothek (steam_api64.dll, libsteam_api.so) wird zur Laufzeit
// geladen: das Projekt baut ohne SDK, und ohne Steam bleiben alle Befehle bei
// 0, waehrend STEAM_ERROR$ den Grund nennt. Gesucht wird in der Umgebung
// GBX_STEAM_LIB und danach neben der EXE.
class Steam {
public:
    ~Steam();

    // Bibliothek laden (ohne SteamAPI_Init).
    bool load(std::string& err);
    // Laden und SteamAPI_Init; appId 0 nimmt steam_appid.txt bzw. SteamAppId.
    bool startup(std::uint32_t appId, std::string& err);
    void shutdown();
    bool ready() const { return ready_; }

    const std::string& lastError() const { return error_; }
    void fail(const std::string& message) { error_ = message; }

    void runCallbacks();
    unsigned long long userId();
    std::string userName();
    int appId();

    int friendCount();
    unsigned long long friendId(int index);
    std::string friendName(int index);
    int friendState(int index);

    int sendPacket(unsigned long long to, const std::string& data, int channel, bool reliable);
    int available(int channel);
    std::string readPacket(int channel, unsigned long long& from);
    unsigned long long lastFrom() const { return lastFrom_; }
    bool acceptSession(unsigned long long user);
    bool closeSession(unsigned long long user);
    int sessionState(unsigned long long user);

    // ---------------------------------------------- Erfolge und Statistiken
    // Freischalten; false, wenn Steam nicht bereit ist oder den Namen nicht
    // kennt. STEAM_STORE schickt die Aenderungen danach an Steam.
    bool unlockAchievement(const std::string& name);
    bool achievementUnlocked(const std::string& name);
    int achievementCount();
    std::string achievementName(int index);
    bool getStatInt(const std::string& name, int& value);
    bool getStatFloat(const std::string& name, float& value);
    bool setStatInt(const std::string& name, int value);
    bool setStatFloat(const std::string& name, float value);
    bool storeStats();

    // ------------------------------------------------------- Ranglisten
    // Eine Rangliste oeffnen (anlegen, falls sie fehlt) und ihre Eintraege
    // lesen. Die Handle bleibt in der Klasse, damit die Punkt-Befehle sie
    // nicht jedes Mal mitbekommen muessen.
    bool openBoard(const std::string& name, bool ascending);
    bool uploadScore(int score, bool keepBest);
    int boardRank() const { return boardRank_; }
    int downloadBoard(int count, int kind);
    int boardCount() const { return static_cast<int>(entries_.size()); }
    std::string boardName(int index);
    std::string boardUser(int index);
    int boardScore(int index);
    int boardPlace(int index);

    // ------------------------------------------------ Steam-Cloud-Speicher
    // Dateien liegen in Steams Cloud-Ordner; der Name ist relativ und ohne
    // Unterordner. Text wird als Bytes geschrieben, STEAM_CLOUD_READ$ liest
    // hoechstens 1 MiB.
    bool cloudWrite(const std::string& file, const std::string& data);
    std::string cloudRead(const std::string& file);
    bool cloudExists(const std::string& file);
    bool cloudDelete(const std::string& file);
    int cloudCount();
    std::string cloudName(int index);
    int cloudSize(const std::string& file);

    // ------------------------------------------------------------ Lobbys
    // typ: 0 privat, 1 nur Freunde, 2 oeffentlich, 3 unsichtbar.
    bool createLobby(int slots, int type);
    bool joinLobby(unsigned long long id);
    void leaveLobby();
    bool inLobby();
    unsigned long long lobbyId() const { return lobby_; }
    unsigned long long lobbyOwner();
    int lobbyMembers();
    unsigned long long lobbyMember(int index);
    std::string lobbyMemberName(int index);
    bool setLobbyData(const std::string& key, const std::string& value);
    std::string lobbyData(const std::string& key);

    // ------------------------------------ Presence, Overlay, Ticket, DLC
    bool setPresence(const std::string& key, const std::string& value);
    std::string presence(const std::string& key);
    bool clearPresence(const std::string& key);
    void overlay(const std::string& page);
    bool overlayEnabled();
    std::string authTicket();
    void cancelTicket();
    int dlcCount();
    bool dlcInstalled(int appid);

private:
    template <typename T>
    bool symbol(T& target, const char* name);
    template <typename T>
    bool versioned(T& target, const char* base, int highest);

    bool bindApi(std::string& err);
    bool hasSession();
    // Wartet auf ein SteamAPICall_t und liest das Ergebnis in out.
    bool waitCall(unsigned long long call, void* out, int size, int callback);

    struct BoardEntry {
        std::string user;
        std::string name;
        int rank = 0;
        int score = 0;
    };

    SDL_SharedObject* lib_ = nullptr;
    bool ready_ = false;
    std::string error_;

    void* user_ = nullptr;
    void* friends_ = nullptr;
    void* networking_ = nullptr;
    void* utils_ = nullptr;
    void* stats_ = nullptr;
    void* storage_ = nullptr;
    void* matchmaking_ = nullptr;
    void* apps_ = nullptr;

    // Die Accessoren (SteamAPI_SteamUser_v023 ...) sind Funktionen: erst ihr
    // Aufruf liefert die Schnittstelle, deren Adresse jede ISteam*-Funktion
    // als erstes Argument erwartet. Vor SteamAPI_Init geben sie nullptr,
    // deshalb werden sie erst in startup() aufgerufen.
    using InterfaceFn = void* (*)();
    InterfaceFn fnUserIface_ = nullptr;
    InterfaceFn fnFriendsIface_ = nullptr;
    InterfaceFn fnNetworkingIface_ = nullptr;
    InterfaceFn fnUtilsIface_ = nullptr;
    InterfaceFn fnStatsIface_ = nullptr;
    InterfaceFn fnStorageIface_ = nullptr;
    InterfaceFn fnMatchmakingIface_ = nullptr;
    InterfaceFn fnAppsIface_ = nullptr;
    void bindInterfaces();

    // SteamAPI_Init ist im SDK inline; exportiert wird SteamAPI_InitFlat mit
    // einem Fehlertext (SteamErrMsg, 1024 Zeichen).
    int (*fnInitFlat_)(char*) = nullptr;
    bool (*fnInitSafe_)() = nullptr;
    void (*fnShutdown_)() = nullptr;
    void (*fnRunCallbacks_)() = nullptr;
    unsigned long long lastFrom_ = 0;

    unsigned long long (*fnGetSteamId_)(void*) = nullptr;
    const char* (*fnPersonaName_)(void*) = nullptr;
    int (*fnFriendCount_)(void*, int) = nullptr;
    unsigned long long (*fnFriendByIndex_)(void*, int, int) = nullptr;
    const char* (*fnFriendName_)(void*, unsigned long long) = nullptr;
    int (*fnFriendState_)(void*, unsigned long long) = nullptr;

    bool (*fnSendP2P_)(void*, unsigned long long, const void*, unsigned, int, int) = nullptr;
    bool (*fnP2PAvailable_)(void*, unsigned*, int) = nullptr;
    bool (*fnReadP2P_)(void*, void*, unsigned, unsigned*, unsigned long long*, int) = nullptr;
    bool (*fnAcceptP2P_)(void*, unsigned long long) = nullptr;
    bool (*fnCloseP2P_)(void*, unsigned long long) = nullptr;
    bool (*fnP2PState_)(void*, unsigned long long, void*) = nullptr;

    unsigned int (*fnAppId_)(void*) = nullptr;

    // Erfolge und Statistiken.
    bool (*fnRequestStats_)(void*) = nullptr;
    bool (*fnGetAchievement_)(void*, const char*) = nullptr;
    bool (*fnSetAchievement_)(void*, const char*) = nullptr;
    bool (*fnStoreStats_)(void*) = nullptr;
    unsigned (*fnAchievementCount_)(void*) = nullptr;
    const char* (*fnAchievementName_)(void*, unsigned) = nullptr;
    bool (*fnGetStatInt_)(void*, const char*, int*) = nullptr;
    bool (*fnGetStatFloat_)(void*, const char*, float*) = nullptr;
    bool (*fnSetStatInt_)(void*, const char*, int) = nullptr;
    bool (*fnSetStatFloat_)(void*, const char*, float) = nullptr;

    // Ranglisten; die Aufrufe sind asynchron und werden in waitCall abgeholt.
    unsigned long long (*fnFindBoard_)(void*, const char*, int, int) = nullptr;
    unsigned long long (*fnUploadScore_)(void*, unsigned long long, int, int, int*, int) = nullptr;
    unsigned long long (*fnDownloadScores_)(void*, unsigned long long, int, int, int, int) = nullptr;
    bool (*fnGetBoardEntry_)(void*, unsigned long long, int, void*, int*, int) = nullptr;

    // Asynchrone Aufrufe abholen.
    bool (*fnIsCallCompleted_)(void*, unsigned long long, bool*) = nullptr;
    bool (*fnGetCallResult_)(void*, unsigned long long, void*, int, int, bool*) = nullptr;

    // Steam-Cloud.
    bool (*fnFileWrite_)(void*, const char*, const void*, int) = nullptr;
    int (*fnFileRead_)(void*, const char*, void*, int) = nullptr;
    bool (*fnFileExists_)(void*, const char*) = nullptr;
    bool (*fnFileDelete_)(void*, const char*) = nullptr;
    int (*fnFileCount_)(void*) = nullptr;
    const char* (*fnFileName_)(void*, int, int*) = nullptr;

    // Lobbys.
    unsigned long long (*fnCreateLobby_)(void*, int, int) = nullptr;
    unsigned long long (*fnJoinLobby_)(void*, unsigned long long) = nullptr;
    void (*fnLeaveLobby_)(void*, unsigned long long) = nullptr;
    unsigned long long (*fnLobbyOwner_)(void*, unsigned long long) = nullptr;
    int (*fnLobbyMembers_)(void*, unsigned long long) = nullptr;
    unsigned long long (*fnLobbyMember_)(void*, unsigned long long, int) = nullptr;
    bool (*fnSetLobbyData_)(void*, unsigned long long, const char*, const char*) = nullptr;
    const char* (*fnGetLobbyData_)(void*, unsigned long long, const char*) = nullptr;

    // Presence, Overlay, Ticket, DLC.
    bool (*fnSetPresence_)(void*, const char*, const char*) = nullptr;
    bool (*fnGetPresence_)(void*, const char*, char*, int) = nullptr;
    void (*fnActivateOverlay_)(void*, const char*) = nullptr;
    bool (*fnOverlayEnabled_)(void*) = nullptr;
    unsigned (*fnGetTicket_)(void*, void*, int, unsigned*) = nullptr;
    void (*fnCancelTicket_)(void*, unsigned) = nullptr;
    int (*fnDlcCount_)(void*) = nullptr;
    bool (*fnDlcInstalled_)(void*, unsigned) = nullptr;

    unsigned long long board_ = 0;
    int boardRank_ = 0;
    std::vector<BoardEntry> entries_;
    unsigned long long lobby_ = 0;
    unsigned ticket_ = 0;
};
