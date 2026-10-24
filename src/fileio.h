// GameBasiX - Dateizugriff fuer FILE_OPEN/FILE_READ.../FILE_WRITE...
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>

// Offene Dateien, getrennt nach Kanal (wie in GLBasic: positive Ganzzahl).
// Modus 1 liest, 0 schreibt neu, -1 haengt an. Die Pfade loest der Aufrufer
// auf, damit Lesen und Schreiben dieselben Regeln wie die uebrigen Befehle
// benutzen.
struct FileSet {
    ~FileSet();

    bool open(int channel, const std::string& path, int mode, std::string& error);
    bool close(int channel);
    bool isOpen(int channel) const;
    bool readable(int channel) const;
    bool writable(int channel) const;

    // Liest bis zu count Bytes; got sagt, wie viele wirklich ankamen.
    bool readBytes(int channel, void* data, std::size_t count, std::size_t& got);
    bool writeBytes(int channel, const void* data, std::size_t count);
    // Eine Zeile ohne "\n" und ohne abschliessendes "\r".
    bool readLine(int channel, std::string& line);

    // origin: -1 = Ende, 0 = Anfang, 1 = aktuelle Position.
    bool seek(int channel, int64_t offset, int origin);
    int64_t position(int channel);
    bool atEnd(int channel);

    void closeAll();

private:
    struct Entry {
        std::fstream stream;
        int mode = 0;
    };
    std::unordered_map<int, Entry> files_;
};
