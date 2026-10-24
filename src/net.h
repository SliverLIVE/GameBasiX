// GameBasiX - Netzwerk fuer NETLISTEN/NETCONNECT und UDPOPEN/UDPSEND
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// Alle Sockets sind nicht blockierend: NETACCEPT, NETRECV$ und UDPRECV$
// kehren sofort zurueck, auch wenn nichts anliegt. NETCONNECT wartet
// hoechstens bis zum angegebenen Zeitlimit.
//
// Handles sind positive Ganzzahlen und gelten fuer TCP und UDP gemeinsam;
// jeder Befehl prueft, ob das Handle zur erwarteten Art passt.
class Net {
public:
    enum class Kind { Listener = 1, Stream = 2, Datagram = 3 };

    ~Net();

    // TCP
    bool listen(int port, int& handle, std::string& err);
    bool connect(const std::string& host, int port, int timeoutMs, int& handle, std::string& err);
    int accept(int listener, std::string& err);                 // 0 = nichts anliegend
    bool sendData(int handle, const std::string& data, int& sent, std::string& err);
    std::string receive(int handle, std::string& err);          // "" = nichts anliegend
    int pending(int handle);
    bool connected(int handle);

    // UDP
    bool openDatagram(int port, int& handle, std::string& err);
    bool sendDatagram(int handle, const std::string& host, int port,
                      const std::string& data, std::string& err);
    std::string receiveDatagram(int handle, std::string& from, std::string& err);

    // Gemeinsam
    Kind kindOf(int handle) const;   // Kind::Listener, wenn unbekannt
    bool known(int handle) const;
    int portOf(int handle) const;
    std::string peerOf(int handle) const;
    std::string localAddress();   // eigene IPv4 fuer die Anzeige (LAN)
    bool closeHandle(int handle);
    void closeAll();

private:
    struct Entry {
        std::intptr_t sock = 0;
        Kind kind = Kind::Listener;
        int port = 0;
        std::string peer;
        bool closed = false;
    };

    int add(std::intptr_t sock, Kind kind, int port, const std::string& peer);
    Entry* find(int handle);
    static bool startup(std::string& err);

    std::unordered_map<int, Entry> entries_;
    int next_ = 1;
};
