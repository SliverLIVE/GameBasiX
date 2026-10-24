// GameBasiX - TCP- und UDP-Sockets fuer die NET*- und UDP*-Befehle
#include "net.h"

// Im Browser gibt es keine rohen Sockets: die Befehle melden das nur, damit
// der HTML5-Player weiterhin baut.
#if defined(__EMSCRIPTEN__)

namespace {
const char* const kNoSockets = "Netzwerk gibt es nur im Desktop-Build";
}

Net::~Net() {}
bool Net::listen(int, int& handle, std::string& err) { handle = 0; err = kNoSockets; return false; }
bool Net::connect(const std::string&, int, int, int& handle, std::string& err) {
    handle = 0;
    err = kNoSockets;
    return false;
}
int Net::accept(int, std::string& err) { err = kNoSockets; return -1; }
bool Net::sendData(int, const std::string&, int& sent, std::string& err) {
    sent = 0;
    err = kNoSockets;
    return false;
}
std::string Net::receive(int, std::string& err) { err = kNoSockets; return ""; }
int Net::pending(int) { return 0; }
bool Net::connected(int) { return false; }
bool Net::openDatagram(int, int& handle, std::string& err) {
    handle = 0;
    err = kNoSockets;
    return false;
}
bool Net::sendDatagram(int, const std::string&, int, const std::string&, std::string& err) {
    err = kNoSockets;
    return false;
}
std::string Net::receiveDatagram(int, std::string&, std::string& err) {
    err = kNoSockets;
    return "";
}
Net::Kind Net::kindOf(int) const { return Kind::Listener; }
bool Net::known(int) const { return false; }
int Net::portOf(int) const { return 0; }
std::string Net::peerOf(int) const { return ""; }
std::string Net::localAddress() { return "127.0.0.1"; }
bool Net::closeHandle(int) { return false; }
void Net::closeAll() {}

#else

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using socket_t = SOCKET;
using io_result = int;
constexpr socket_t kInvalid = INVALID_SOCKET;

int socketError() { return WSAGetLastError(); }
void closeSocket(socket_t s) { closesocket(s); }

std::string errorString(int code) {
    char* buffer = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, static_cast<DWORD>(code), 0,
                               reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string text = len && buffer ? std::string(buffer, len) : "Fehler " + std::to_string(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}
#else
using socket_t = int;
using io_result = ssize_t;
constexpr socket_t kInvalid = -1;

int socketError() { return errno; }
void closeSocket(socket_t s) { ::close(s); }

std::string errorString(int code) { return std::strerror(code); }
#endif

bool wouldBlock(int code) {
#if defined(_WIN32)
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEINVAL;
#else
    return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS;
#endif
}

bool interrupted(int code) {
#if defined(_WIN32)
    return code == WSAEINTR;
#else
    return code == EINTR;
#endif
}

std::string describe(const char* what, int code) {
    return std::string(what) + ": " + errorString(code);
}

bool setNonBlocking(socket_t s) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void setNoDelay(socket_t s) {
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
}

bool waitWritable(socket_t s, int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
#if defined(_WIN32)
    int ready = select(0, nullptr, &set, nullptr, &tv);
#else
    int ready = select(s + 1, nullptr, &set, nullptr, &tv);
#endif
    return ready > 0;
}

// Textform einer Adresse fuer NETPEER$/UDPFROM$.
std::string formatAddress(const sockaddr* addr) {
    char host[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    if (addr->sa_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(addr);
        inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host));
        port = ntohs(v4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(addr);
        inet_ntop(AF_INET6, &v6->sin6_addr, host, sizeof(host));
        port = ntohs(v6->sin6_port);
    }
    return std::string(host) + ":" + std::to_string(port);
}

struct Resolved {
    ~Resolved() {
        if (list) freeaddrinfo(list);
    }
    addrinfo* list = nullptr;
    addrinfo* first() const { return list; }
};

bool resolve(const std::string& host, int port, int type, Resolved& out, std::string& err) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;
    hints.ai_flags = AI_ADDRCONFIG;
    addrinfo* list = nullptr;
    std::string service = std::to_string(port);
    int rc = getaddrinfo(host.empty() ? "127.0.0.1" : host.c_str(), service.c_str(), &hints, &list);
    if (rc != 0 || !list) {
#if defined(_WIN32)
        err = "Adresse nicht gefunden: " + host;
#else
        err = "Adresse nicht gefunden: " + host + " (" + gai_strerror(rc) + ")";
#endif
        if (list) freeaddrinfo(list);
        return false;
    }
    out.list = list;
    return true;
}

}  // namespace

Net::~Net() { closeAll(); }

bool Net::startup(std::string& err) {
#if defined(_WIN32)
    static bool ready = false;
    if (ready) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        err = "Winsock startet nicht: " + errorString(socketError());
        return false;
    }
    ready = true;
#else
    (void)err;
#endif
    return true;
}

int Net::add(std::intptr_t sock, Kind kind, int port, const std::string& peer) {
    const int handle = next_++;
    Entry entry;
    entry.sock = sock;
    entry.kind = kind;
    entry.port = port;
    entry.peer = peer;
    entries_[handle] = entry;
    return handle;
}

Net::Entry* Net::find(int handle) {
    auto it = entries_.find(handle);
    return it == entries_.end() ? nullptr : &it->second;
}

bool Net::known(int handle) const { return entries_.count(handle) != 0; }

Net::Kind Net::kindOf(int handle) const {
    auto it = entries_.find(handle);
    return it == entries_.end() ? Kind::Listener : it->second.kind;
}

int Net::portOf(int handle) const {
    auto it = entries_.find(handle);
    return it == entries_.end() ? 0 : it->second.port;
}

std::string Net::peerOf(int handle) const {
    auto it = entries_.find(handle);
    return it == entries_.end() ? std::string() : it->second.peer;
}

std::string Net::localAddress() {
    std::string err;
    if (!startup(err)) return "127.0.0.1";
    Resolved resolved;
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char name[256] = {0};
    if (gethostname(name, sizeof(name)) != 0) return "127.0.0.1";
    if (getaddrinfo(name, nullptr, &hints, &resolved.list) != 0) return "127.0.0.1";
    for (addrinfo* it = resolved.first(); it; it = it->ai_next) {
        char host[INET_ADDRSTRLEN] = {0};
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
        inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host));
        std::string text = host;
        if (text.rfind("127.", 0) != 0) return text;
    }
    return "127.0.0.1";
}

bool Net::listen(int port, int& handle, std::string& err) {
    handle = 0;
    if (!startup(err)) return false;
    if (port < 0 || port > 65535) {
        err = "Port ausserhalb von 0..65535: " + std::to_string(port);
        return false;
    }
    Resolved resolved;
    if (!resolve("0.0.0.0", port, SOCK_STREAM, resolved, err)) return false;

    for (addrinfo* it = resolved.first(); it; it = it->ai_next) {
        socket_t s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == kInvalid) continue;
        // Windows: ein zweiter Server auf demselben Port soll scheitern, statt
        // den ersten zu verdraengen. POSIX: SO_REUSEADDR fuer schnelles Neustarten.
#if defined(_WIN32)
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#endif
        if (bind(s, it->ai_addr, static_cast<int>(it->ai_addrlen)) != 0) {
            err = describe("Port belegt", socketError());
            closeSocket(s);
            continue;
        }
        if (::listen(s, 16) != 0) {
            err = describe("listen fehlgeschlagen", socketError());
            closeSocket(s);
            continue;
        }
        if (!setNonBlocking(s)) {
            err = describe("Socket laesst sich nicht umstellen", socketError());
            closeSocket(s);
            continue;
        }
        sockaddr_storage bound;
        socklen_t boundLen = sizeof(bound);
        int boundPort = port;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
            if (bound.ss_family == AF_INET)
                boundPort = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
            else if (bound.ss_family == AF_INET6)
                boundPort = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);
        }
        handle = add(static_cast<std::intptr_t>(s), Kind::Listener, boundPort, "");
        return true;
    }
    if (err.empty()) err = "Kein Port verfuegbar";
    return false;
}

bool Net::connect(const std::string& host, int port, int timeoutMs, int& handle, std::string& err) {
    handle = 0;
    if (!startup(err)) return false;
    if (port < 1 || port > 65535) {
        err = "Port ausserhalb von 1..65535: " + std::to_string(port);
        return false;
    }
    Resolved resolved;
    if (!resolve(host, port, SOCK_STREAM, resolved, err)) return false;

    for (addrinfo* it = resolved.first(); it; it = it->ai_next) {
        socket_t s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == kInvalid) continue;
        if (!setNonBlocking(s)) {
            closeSocket(s);
            continue;
        }
        int rc = ::connect(s, it->ai_addr, static_cast<int>(it->ai_addrlen));
        if (rc != 0) {
            int code = socketError();
            if (!wouldBlock(code)) {
                err = describe("Verbindung fehlgeschlagen", code);
                closeSocket(s);
                continue;
            }
            if (!waitWritable(s, timeoutMs)) {
                err = "Zeitueberschreitung bei der Verbindung zu " + host;
                closeSocket(s);
                continue;
            }
            int soError = 0;
            socklen_t len = sizeof(soError);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
            if (soError != 0) {
                err = describe("Verbindung abgelehnt", soError);
                closeSocket(s);
                continue;
            }
        }
        setNoDelay(s);
        handle = add(static_cast<std::intptr_t>(s), Kind::Stream, port, host + ":" + std::to_string(port));
        return true;
    }
    if (err.empty()) err = "Verbindung fehlgeschlagen";
    return false;
}

int Net::accept(int listener, std::string& err) {
    Entry* entry = find(listener);
    if (!entry || entry->kind != Kind::Listener) {
        err = "Kein wartender Server auf Handle " + std::to_string(listener);
        return -1;
    }
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    socket_t s = ::accept(static_cast<socket_t>(entry->sock),
                          reinterpret_cast<sockaddr*>(&addr), &len);
    if (s == kInvalid) {
        int code = socketError();
        if (wouldBlock(code)) return 0;
        err = describe("accept fehlgeschlagen", code);
        return -1;
    }
    setNonBlocking(s);
    setNoDelay(s);
    return add(static_cast<std::intptr_t>(s), Kind::Stream, 0, formatAddress(reinterpret_cast<sockaddr*>(&addr)));
}

bool Net::sendData(int handle, const std::string& data, int& sent, std::string& err) {
    sent = 0;
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Stream) {
        err = "Kein Client auf Handle " + std::to_string(handle);
        return false;
    }
    if (entry->closed) {
        err = "Verbindung ist bereits geschlossen";
        return false;
    }
    socket_t s = static_cast<socket_t>(entry->sock);
    const char* cursor = data.data();
    size_t left = data.size();
    while (left > 0) {
#if defined(_WIN32)
        io_result written = ::send(s, cursor, static_cast<int>(left), 0);
#else
        io_result written = ::send(s, cursor, left, MSG_NOSIGNAL);
#endif
        if (written > 0) {
            cursor += written;
            left -= static_cast<size_t>(written);
            sent += static_cast<int>(written);
            continue;
        }
        int code = socketError();
        if (interrupted(code)) continue;
        if (wouldBlock(code)) {
            if (!waitWritable(s, 3000)) {
                err = "Senden dauert zu lange (Gegenstelle liest nicht)";
                return sent > 0;
            }
            continue;
        }
        err = describe("Senden fehlgeschlagen", code);
        entry->closed = true;
        return sent > 0;
    }
    return true;
}

std::string Net::receive(int handle, std::string& err) {
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Stream) {
        err = "Kein Client auf Handle " + std::to_string(handle);
        return "";
    }
    if (entry->closed) return "";
    socket_t s = static_cast<socket_t>(entry->sock);
    std::string out;
    char buffer[16384];
    constexpr size_t kMax = 1u << 20;
    while (out.size() < kMax) {
        io_result got = ::recv(s, buffer, sizeof(buffer), 0);
        if (got > 0) {
            out.append(buffer, static_cast<size_t>(got));
            continue;
        }
        if (got == 0) {
            entry->closed = true;
            break;
        }
        int code = socketError();
        if (interrupted(code)) continue;
        if (wouldBlock(code)) break;
        err = describe("Empfangen fehlgeschlagen", code);
        entry->closed = true;
        break;
    }
    return out;
}

int Net::pending(int handle) {
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Stream || entry->closed) return 0;
    socket_t s = static_cast<socket_t>(entry->sock);
#if defined(_WIN32)
    u_long count = 0;
    if (ioctlsocket(s, FIONREAD, &count) != 0) return 0;
    return static_cast<int>(count);
#else
    int count = 0;
    if (ioctl(s, FIONREAD, &count) != 0) return 0;
    return count;
#endif
}

bool Net::connected(int handle) {
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Stream) return false;
    if (entry->closed) return false;
    socket_t s = static_cast<socket_t>(entry->sock);
    char probe = 0;
#if defined(_WIN32)
    io_result got = ::recv(s, &probe, 1, MSG_PEEK);
#else
    io_result got = ::recv(s, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
#endif
    if (got == 0) {
        entry->closed = true;
        return false;
    }
    if (got < 0) {
        int code = socketError();
        if (wouldBlock(code)) return true;
        if (interrupted(code)) return true;
        entry->closed = true;
        return false;
    }
    return true;
}

bool Net::openDatagram(int port, int& handle, std::string& err) {
    handle = 0;
    if (!startup(err)) return false;
    if (port < 0 || port > 65535) {
        err = "Port ausserhalb von 0..65535: " + std::to_string(port);
        return false;
    }
    Resolved resolved;
    if (!resolve("0.0.0.0", port, SOCK_DGRAM, resolved, err)) return false;
    for (addrinfo* it = resolved.first(); it; it = it->ai_next) {
        socket_t s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == kInvalid) continue;
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
        if (bind(s, it->ai_addr, static_cast<int>(it->ai_addrlen)) != 0) {
            err = describe("UDP-Port belegt", socketError());
            closeSocket(s);
            continue;
        }
        if (!setNonBlocking(s)) {
            err = describe("Socket laesst sich nicht umstellen", socketError());
            closeSocket(s);
            continue;
        }
        sockaddr_storage bound;
        socklen_t len = sizeof(bound);
        int boundPort = port;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            if (bound.ss_family == AF_INET)
                boundPort = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
            else if (bound.ss_family == AF_INET6)
                boundPort = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);
        }
        handle = add(static_cast<std::intptr_t>(s), Kind::Datagram, boundPort, "");
        return true;
    }
    if (err.empty()) err = "Kein UDP-Port verfuegbar";
    return false;
}

bool Net::sendDatagram(int handle, const std::string& host, int port,
                       const std::string& data, std::string& err) {
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Datagram) {
        err = "Kein UDP-Socket auf Handle " + std::to_string(handle);
        return false;
    }
    if (port < 1 || port > 65535) {
        err = "Port ausserhalb von 1..65535: " + std::to_string(port);
        return false;
    }
    Resolved resolved;
    if (!resolve(host, port, SOCK_DGRAM, resolved, err)) return false;
    socket_t s = static_cast<socket_t>(entry->sock);
    for (addrinfo* it = resolved.first(); it; it = it->ai_next) {
        io_result written = ::sendto(s, data.data(), data.size(), 0, it->ai_addr,
                                     static_cast<int>(it->ai_addrlen));
        if (written >= 0) return true;
        int code = socketError();
        if (interrupted(code)) continue;
        err = describe("UDP senden fehlgeschlagen", code);
        return false;
    }
    if (err.empty()) err = "UDP senden fehlgeschlagen";
    return false;
}

std::string Net::receiveDatagram(int handle, std::string& from, std::string& err) {
    Entry* entry = find(handle);
    if (!entry || entry->kind != Kind::Datagram) {
        err = "Kein UDP-Socket auf Handle " + std::to_string(handle);
        return "";
    }
    socket_t s = static_cast<socket_t>(entry->sock);
    std::vector<char> buffer(65536);
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    io_result got = ::recvfrom(s, buffer.data(), buffer.size(), 0,
                               reinterpret_cast<sockaddr*>(&addr), &len);
    if (got < 0) {
        int code = socketError();
        if (wouldBlock(code)) return "";
        if (interrupted(code)) return "";
        err = describe("UDP empfangen fehlgeschlagen", code);
        return "";
    }
    from = formatAddress(reinterpret_cast<sockaddr*>(&addr));
    return std::string(buffer.data(), static_cast<size_t>(got));
}

bool Net::closeHandle(int handle) {
    auto it = entries_.find(handle);
    if (it == entries_.end()) return false;
    closeSocket(static_cast<socket_t>(it->second.sock));
    entries_.erase(it);
    return true;
}

void Net::closeAll() {
    for (auto& item : entries_)
        closeSocket(static_cast<socket_t>(item.second.sock));
    entries_.clear();
}

#endif  // __EMSCRIPTEN__
