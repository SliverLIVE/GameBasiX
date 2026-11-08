// GameBasiX - Steam module: the STEAM_* commands
//
// Registered with GBX_INLINE_FN, so they look and behave like the built-ins
// they replace; argument counts and defaults are unchanged.
#ifndef GBX_STEAM_MODULE_COMMANDS_H
#define GBX_STEAM_MODULE_COMMANDS_H

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <string>

namespace steamgbx {

inline Steam& api() {
    static Steam instance;
    return instance;
}

inline std::string textArg(const GbxValue* args, int argc, int index) {
    return gbxinline::argText(args, argc, index);
}

inline int intArg(const GbxValue* args, int argc, int index, int fallback = 0) {
    if (index >= argc) return fallback;
    return static_cast<int>(gbxinline::argNum(args, argc, index, fallback));
}

inline bool flagArg(const GbxValue* args, int argc, int index, bool fallback) {
    if (index >= argc) return fallback;
    return gbxinline::argNum(args, argc, index, 0.0) != 0.0;
}

inline int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

inline void retBool(GbxValue* ret, bool value) {
    gbxinline::retNum(ret, value ? 1.0 : 0.0);
}

inline std::string idText(unsigned long long id) {
    return id == 0 ? std::string() : std::to_string(id);
}

inline unsigned long long steamIdArg(const GbxValue* args, int argc, int index) {
    const std::string text = gbxinline::argText(args, argc, index);
    std::string digits;
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) digits.push_back(ch);
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long id = std::strtoull(digits.c_str(), &end, 10);
    if (digits.empty() || end == digits.c_str() || errno != 0) {
        api().fail("SteamID must be a number: " + text);
        return 0;
    }
    return id;
}

static void steam_init(const GbxValue* args, int argc, GbxValue* ret) {
    const unsigned appId = static_cast<unsigned>(intArg(args, argc, 0, 0));
    std::string err;
    if (!api().startup(appId, err)) {
        api().fail(err);
        gbxinline::retNum(ret, 0);
        return;
    }
    gbxinline::retNum(ret, 1);
}

static void steam_shutdown(const GbxValue*, int, GbxValue* ret) {
    api().shutdown();
    gbxinline::retNum(ret, 1);
}

static void steam_run(const GbxValue*, int, GbxValue* ret) {
    api().runCallbacks();
    retBool(ret, api().ready());
}

static void steam_ready(const GbxValue*, int, GbxValue* ret) { retBool(ret, api().ready()); }
static void steam_name(const GbxValue*, int, GbxValue* ret) { gbxinline::retText(ret, api().userName()); }
static void steam_id(const GbxValue*, int, GbxValue* ret) { gbxinline::retText(ret, idText(api().userId())); }
static void steam_app_id(const GbxValue*, int, GbxValue* ret) { gbxinline::retNum(ret, api().appId()); }
static void steam_friends(const GbxValue*, int, GbxValue* ret) { gbxinline::retNum(ret, api().friendCount()); }

static void steam_friend_id(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, idText(api().friendId(intArg(args, argc, 0))));
}

static void steam_friend_name(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().friendName(intArg(args, argc, 0)));
}

static void steam_friend_state(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().friendState(intArg(args, argc, 0)));
}

static void steam_p2p_send(const GbxValue* args, int argc, GbxValue* ret) {
    const unsigned long long to = steamIdArg(args, argc, 0);
    const std::string data = textArg(args, argc, 1);
    const int channel = clampInt(intArg(args, argc, 2, 0), 0, 255);
    const bool reliable = flagArg(args, argc, 3, true);
    const int sent = api().sendPacket(to, data, channel, reliable);
    if (sent == 0 && !api().ready()) api().fail("Steam is not ready - call STEAM_INIT first");
    gbxinline::retNum(ret, sent);
}

static void steam_p2p_available(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().available(clampInt(intArg(args, argc, 0, 0), 0, 255)));
}

static void steam_p2p_recv(const GbxValue* args, int argc, GbxValue* ret) {
    unsigned long long from = 0;
    gbxinline::retText(ret, api().readPacket(clampInt(intArg(args, argc, 0, 0), 0, 255), from));
}

static void steam_p2p_from(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retText(ret, idText(api().lastFrom()));
}

static void steam_p2p_accept(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().acceptSession(steamIdArg(args, argc, 0)));
}

static void steam_p2p_close(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().closeSession(steamIdArg(args, argc, 0)));
}

static void steam_p2p_state(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().sessionState(steamIdArg(args, argc, 0)));
}

static void steam_error(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retText(ret, api().lastError());
}

static void steam_achievement(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().unlockAchievement(textArg(args, argc, 0)));
}

static void steam_achieved(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().achievementUnlocked(textArg(args, argc, 0)));
}

static void steam_achievements(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().achievementCount());
}

static void steam_achievement_name(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().achievementName(intArg(args, argc, 0)));
}

static void steam_stat(const GbxValue* args, int argc, GbxValue* ret) {
    int value = 0;
    api().getStatInt(textArg(args, argc, 0), value);
    gbxinline::retNum(ret, value);
}

static void steam_stat_f(const GbxValue* args, int argc, GbxValue* ret) {
    float value = 0.0f;
    api().getStatFloat(textArg(args, argc, 0), value);
    gbxinline::retNum(ret, static_cast<double>(value));
}

static void steam_stat_set(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().setStatInt(textArg(args, argc, 0), intArg(args, argc, 1)));
}

static void steam_stat_setf(const GbxValue* args, int argc, GbxValue* ret) {
    const float value = static_cast<float>(gbxinline::argNum(args, argc, 1, 0.0));
    retBool(ret, api().setStatFloat(textArg(args, argc, 0), value));
}

static void steam_store(const GbxValue*, int, GbxValue* ret) { retBool(ret, api().storeStats()); }

static void steam_board(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().openBoard(textArg(args, argc, 0), flagArg(args, argc, 1, false)));
}

static void steam_board_score(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().uploadScore(intArg(args, argc, 0), flagArg(args, argc, 1, true)));
}

static void steam_board_rank(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().boardRank());
}

static void steam_board_top(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().downloadBoard(intArg(args, argc, 0, 10), intArg(args, argc, 1, 0)));
}

static void steam_board_count(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().boardCount());
}

static void steam_board_name(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().boardName(intArg(args, argc, 0)));
}

static void steam_board_id(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().boardUser(intArg(args, argc, 0)));
}

static void steam_board_points(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().boardScore(intArg(args, argc, 0)));
}

static void steam_board_place(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().boardPlace(intArg(args, argc, 0)));
}

static void steam_cloud_write(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().cloudWrite(textArg(args, argc, 0), textArg(args, argc, 1)));
}

static void steam_cloud_read(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().cloudRead(textArg(args, argc, 0)));
}

static void steam_cloud_exists(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().cloudExists(textArg(args, argc, 0)));
}

static void steam_cloud_delete(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().cloudDelete(textArg(args, argc, 0)));
}

static void steam_cloud_files(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().cloudCount());
}

static void steam_cloud_name(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().cloudName(intArg(args, argc, 0)));
}

static void steam_cloud_size(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, api().cloudSize(textArg(args, argc, 0)));
}

static void steam_lobby_create(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().createLobby(intArg(args, argc, 0, 4), intArg(args, argc, 1, 2)));
}

static void steam_lobby_join(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().joinLobby(steamIdArg(args, argc, 0)));
}

static void steam_lobby_joined(const GbxValue*, int, GbxValue* ret) { retBool(ret, api().inLobby()); }

static void steam_lobby_id(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retText(ret, idText(api().lobbyId()));
}

static void steam_lobby_leave(const GbxValue*, int, GbxValue* ret) {
    api().leaveLobby();
    gbxinline::retNum(ret, 1);
}

static void steam_lobby_owner(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retText(ret, idText(api().lobbyOwner()));
}

static void steam_lobby_members(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().lobbyMembers());
}

static void steam_lobby_member_id(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, idText(api().lobbyMember(intArg(args, argc, 0))));
}

static void steam_lobby_member_name(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().lobbyMemberName(intArg(args, argc, 0)));
}

static void steam_lobby_set(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().setLobbyData(textArg(args, argc, 0), textArg(args, argc, 1)));
}

static void steam_lobby_get(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, api().lobbyData(textArg(args, argc, 0)));
}

// STEAM_PRESENCE and STEAM_PRESENCE$ share one INLINE function: the registry
// keys functions by name without the type suffix, so the argument count
// decides whether this is the setter or the getter.
static void steam_presence(const GbxValue* args, int argc, GbxValue* ret) {
    if (argc >= 2) {
        retBool(ret, api().setPresence(textArg(args, argc, 0), textArg(args, argc, 1)));
        return;
    }
    gbxinline::retText(ret, api().presence(textArg(args, argc, 0)));
}

static void steam_presence_clear(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().clearPresence(textArg(args, argc, 0)));
}

static void steam_overlay(const GbxValue* args, int argc, GbxValue* ret) {
    api().overlay(argc > 0 ? textArg(args, argc, 0) : std::string());
    gbxinline::retNum(ret, 1);
}

static void steam_overlay_on(const GbxValue*, int, GbxValue* ret) {
    retBool(ret, api().overlayEnabled());
}

static void steam_ticket(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retText(ret, api().authTicket());
}

static void steam_ticket_cancel(const GbxValue*, int, GbxValue* ret) {
    api().cancelTicket();
    gbxinline::retNum(ret, 1);
}

static void steam_dlc_count(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, api().dlcCount());
}

static void steam_dlc(const GbxValue* args, int argc, GbxValue* ret) {
    retBool(ret, api().dlcInstalled(intArg(args, argc, 0)));
}

GBX_INLINE_FN("STEAM_INIT", 0, 1, steam_init);
GBX_INLINE_FN("STEAM_SHUTDOWN", 0, 0, steam_shutdown);
GBX_INLINE_FN("STEAM_RUN", 0, 0, steam_run);
GBX_INLINE_FN("STEAM_READY", 0, 0, steam_ready);
GBX_INLINE_FN("STEAM_NAME$", 0, 0, steam_name);
GBX_INLINE_FN("STEAM_ID$", 0, 0, steam_id);
GBX_INLINE_FN("STEAM_APP_ID", 0, 0, steam_app_id);
GBX_INLINE_FN("STEAM_FRIENDS", 0, 0, steam_friends);
GBX_INLINE_FN("STEAM_FRIEND_ID$", 1, 1, steam_friend_id);
GBX_INLINE_FN("STEAM_FRIEND_NAME$", 1, 1, steam_friend_name);
GBX_INLINE_FN("STEAM_FRIEND_STATE", 1, 1, steam_friend_state);
GBX_INLINE_FN("STEAM_P2P_SEND", 2, 4, steam_p2p_send);
GBX_INLINE_FN("STEAM_P2P_AVAILABLE", 0, 1, steam_p2p_available);
GBX_INLINE_FN("STEAM_P2P_RECV$", 0, 1, steam_p2p_recv);
GBX_INLINE_FN("STEAM_P2P_FROM$", 0, 0, steam_p2p_from);
GBX_INLINE_FN("STEAM_P2P_ACCEPT", 1, 1, steam_p2p_accept);
GBX_INLINE_FN("STEAM_P2P_CLOSE", 1, 1, steam_p2p_close);
GBX_INLINE_FN("STEAM_P2P_STATE", 1, 1, steam_p2p_state);
GBX_INLINE_FN("STEAM_ERROR$", 0, 0, steam_error);
GBX_INLINE_FN("STEAM_ACHIEVEMENT", 1, 1, steam_achievement);
GBX_INLINE_FN("STEAM_ACHIEVED", 1, 1, steam_achieved);
GBX_INLINE_FN("STEAM_ACHIEVEMENTS", 0, 0, steam_achievements);
GBX_INLINE_FN("STEAM_ACHIEVEMENT_NAME$", 1, 1, steam_achievement_name);
GBX_INLINE_FN("STEAM_STAT", 1, 1, steam_stat);
GBX_INLINE_FN("STEAM_STAT_F", 1, 1, steam_stat_f);
GBX_INLINE_FN("STEAM_STAT_SET", 2, 2, steam_stat_set);
GBX_INLINE_FN("STEAM_STAT_SETF", 2, 2, steam_stat_setf);
GBX_INLINE_FN("STEAM_STORE", 0, 0, steam_store);
GBX_INLINE_FN("STEAM_BOARD", 1, 2, steam_board);
GBX_INLINE_FN("STEAM_BOARD_SCORE", 1, 2, steam_board_score);
GBX_INLINE_FN("STEAM_BOARD_RANK", 0, 0, steam_board_rank);
GBX_INLINE_FN("STEAM_BOARD_TOP", 0, 2, steam_board_top);
GBX_INLINE_FN("STEAM_BOARD_COUNT", 0, 0, steam_board_count);
GBX_INLINE_FN("STEAM_BOARD_NAME$", 1, 1, steam_board_name);
GBX_INLINE_FN("STEAM_BOARD_ID$", 1, 1, steam_board_id);
GBX_INLINE_FN("STEAM_BOARD_POINTS", 1, 1, steam_board_points);
GBX_INLINE_FN("STEAM_BOARD_PLACE", 1, 1, steam_board_place);
GBX_INLINE_FN("STEAM_CLOUD_WRITE", 2, 2, steam_cloud_write);
GBX_INLINE_FN("STEAM_CLOUD_READ$", 1, 1, steam_cloud_read);
GBX_INLINE_FN("STEAM_CLOUD_EXISTS", 1, 1, steam_cloud_exists);
GBX_INLINE_FN("STEAM_CLOUD_DELETE", 1, 1, steam_cloud_delete);
GBX_INLINE_FN("STEAM_CLOUD_FILES", 0, 0, steam_cloud_files);
GBX_INLINE_FN("STEAM_CLOUD_NAME$", 1, 1, steam_cloud_name);
GBX_INLINE_FN("STEAM_CLOUD_SIZE", 1, 1, steam_cloud_size);
GBX_INLINE_FN("STEAM_LOBBY_CREATE", 0, 2, steam_lobby_create);
GBX_INLINE_FN("STEAM_LOBBY_JOIN", 1, 1, steam_lobby_join);
GBX_INLINE_FN("STEAM_LOBBY_JOINED", 0, 0, steam_lobby_joined);
GBX_INLINE_FN("STEAM_LOBBY_ID$", 0, 0, steam_lobby_id);
GBX_INLINE_FN("STEAM_LOBBY_LEAVE", 0, 0, steam_lobby_leave);
GBX_INLINE_FN("STEAM_LOBBY_OWNER$", 0, 0, steam_lobby_owner);
GBX_INLINE_FN("STEAM_LOBBY_MEMBERS", 0, 0, steam_lobby_members);
GBX_INLINE_FN("STEAM_LOBBY_MEMBER_ID$", 1, 1, steam_lobby_member_id);
GBX_INLINE_FN("STEAM_LOBBY_MEMBER_NAME$", 1, 1, steam_lobby_member_name);
GBX_INLINE_FN("STEAM_LOBBY_SET", 2, 2, steam_lobby_set);
GBX_INLINE_FN("STEAM_LOBBY_GET$", 1, 1, steam_lobby_get);
GBX_INLINE_FN("STEAM_PRESENCE", 1, 2, steam_presence);
GBX_INLINE_FN("STEAM_PRESENCE_CLEAR", 1, 1, steam_presence_clear);
GBX_INLINE_FN("STEAM_OVERLAY", 0, 1, steam_overlay);
GBX_INLINE_FN("STEAM_OVERLAY_ON", 0, 0, steam_overlay_on);
GBX_INLINE_FN("STEAM_TICKET$", 0, 0, steam_ticket);
GBX_INLINE_FN("STEAM_TICKET_CANCEL", 0, 0, steam_ticket_cancel);
GBX_INLINE_FN("STEAM_DLC_COUNT", 0, 0, steam_dlc_count);
GBX_INLINE_FN("STEAM_DLC", 1, 1, steam_dlc);

}  // namespace steamgbx

#endif  // GBX_STEAM_MODULE_COMMANDS_H
