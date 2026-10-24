// Mock der Steam-Flat-API fuer tools/check_steam.py.
//
// Bildet genau die Symbole nach, die GameBasiX dynamisch laedt: Identitaet,
// Freundesliste und P2P-Pakete. So laesst sich der komplette Pfad ohne
// installiertes Steam pruefen. Keine C++-Standardbibliothek, damit die DLL
// ohne zusaetzliche Laufzeit-DLLs geladen werden kann.
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

bool ifaceIs(void* self, std::uintptr_t expected) {
    return reinterpret_cast<std::uintptr_t>(self) == expected;
}

char g_packet[kPacketSize];
unsigned g_packetLength = 0;
int g_runCallbacks = 0;

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
    return id == kFriend ? "Mock Friend" : "";
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
