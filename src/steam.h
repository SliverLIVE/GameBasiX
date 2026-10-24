// GameBasiX - Steamworks ueber die stabile C-Flat-API
#pragma once

#include <cstdint>
#include <string>

struct SDL_SharedObject;

// Die Steam-Bibliothek (steam_api64.dll, libsteam_api.so) wird zur Laufzeit
// geladen: das Projekt baut ohne SDK, und ohne Steam bleiben alle Befehle bei
// 0, waehrend STEAMERROR$ den Grund nennt. Gesucht wird in der Umgebung
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

private:
    template <typename T>
    bool symbol(T& target, const char* name);
    template <typename T>
    bool versioned(T& target, const char* base, int highest);

    bool bindApi(std::string& err);
    bool hasSession();

    SDL_SharedObject* lib_ = nullptr;
    bool ready_ = false;
    std::string error_;

    void* user_ = nullptr;
    void* friends_ = nullptr;
    void* networking_ = nullptr;
    void* utils_ = nullptr;

    // Die Accessoren (SteamAPI_SteamUser_v023 ...) sind Funktionen: erst ihr
    // Aufruf liefert die Schnittstelle, deren Adresse jede ISteam*-Funktion
    // als erstes Argument erwartet. Vor SteamAPI_Init geben sie nullptr,
    // deshalb werden sie erst in startup() aufgerufen.
    using InterfaceFn = void* (*)();
    InterfaceFn fnUserIface_ = nullptr;
    InterfaceFn fnFriendsIface_ = nullptr;
    InterfaceFn fnNetworkingIface_ = nullptr;
    InterfaceFn fnUtilsIface_ = nullptr;
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
};
