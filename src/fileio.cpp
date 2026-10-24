// GameBasiX - Dateizugriff
#include "fileio.h"

#include <filesystem>
#include <ios>
#include <system_error>

namespace fs = std::filesystem;

FileSet::~FileSet() {
    closeAll();
}

bool FileSet::open(int channel, const std::string& path, int mode, std::string& error) {
    if (channel <= 0) {
        error = "Kanal muss groesser als 0 sein";
        return false;
    }
    if (files_.find(channel) != files_.end()) {
        error = "Kanal " + std::to_string(channel) + " ist bereits offen";
        return false;
    }

    std::ios::openmode flags = std::ios::binary;
    if (mode == 1) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(path), ec)) {
            error = "Datei nicht gefunden: " + path;
            return false;
        }
        flags |= std::ios::in;
    } else if (mode == 0) {
        flags |= std::ios::out | std::ios::trunc;
    } else if (mode == -1) {
        flags |= std::ios::out | std::ios::app;
    } else {
        error = "Modus muss 1 (lesen), 0 (schreiben) oder -1 (anhaengen) sein";
        return false;
    }

    Entry entry;
    entry.mode = mode;
    entry.stream.open(path, flags);
    if (!entry.stream.is_open()) {
        error = "Datei laesst sich nicht oeffnen: " + path;
        return false;
    }
    files_.emplace(channel, std::move(entry));
    return true;
}

bool FileSet::close(int channel) {
    auto it = files_.find(channel);
    if (it == files_.end()) return false;
    if (it->second.stream.is_open()) {
        it->second.stream.flush();
        it->second.stream.close();
    }
    files_.erase(it);
    return true;
}

bool FileSet::isOpen(int channel) const {
    return files_.find(channel) != files_.end();
}

bool FileSet::readable(int channel) const {
    auto it = files_.find(channel);
    return it != files_.end() && it->second.mode == 1;
}

bool FileSet::writable(int channel) const {
    auto it = files_.find(channel);
    return it != files_.end() && it->second.mode != 1;
}

bool FileSet::readBytes(int channel, void* data, std::size_t count, std::size_t& got) {
    got = 0;
    auto it = files_.find(channel);
    if (it == files_.end() || it->second.mode != 1) return false;
    if (count == 0) return true;
    it->second.stream.read(static_cast<char*>(data), static_cast<std::streamsize>(count));
    got = static_cast<std::size_t>(it->second.stream.gcount());
    return got == count;
}

bool FileSet::writeBytes(int channel, const void* data, std::size_t count) {
    auto it = files_.find(channel);
    if (it == files_.end() || it->second.mode == 1) return false;
    if (count == 0) return true;
    it->second.stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
    return it->second.stream.good();
}

bool FileSet::readLine(int channel, std::string& line) {
    line.clear();
    auto it = files_.find(channel);
    if (it == files_.end() || it->second.mode != 1) return false;
    std::getline(it->second.stream, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return !it->second.stream.fail() || !line.empty();
}

bool FileSet::seek(int channel, int64_t offset, int origin) {
    auto it = files_.find(channel);
    if (it == files_.end()) return false;
    std::ios_base::seekdir dir = std::ios::beg;
    if (origin == -1) {
        dir = std::ios::end;
    } else if (origin == 1) {
        dir = std::ios::cur;
    } else if (origin != 0) {
        return false;
    }
    it->second.stream.clear();
    if (it->second.mode == 1) {
        it->second.stream.seekg(static_cast<std::streamoff>(offset), dir);
    } else {
        it->second.stream.seekp(static_cast<std::streamoff>(offset), dir);
    }
    return !it->second.stream.fail();
}

int64_t FileSet::position(int channel) {
    auto it = files_.find(channel);
    if (it == files_.end()) return -1;
    std::streampos pos = it->second.mode == 1 ? it->second.stream.tellg() : it->second.stream.tellp();
    if (pos == std::streampos(-1)) return -1;
    return static_cast<int64_t>(pos);
}

bool FileSet::atEnd(int channel) {
    auto it = files_.find(channel);
    if (it == files_.end() || it->second.mode != 1) return false;
    // peek() setzt am Ende das eof-Bit; das wieder loeschen, damit
    // FILE_POSITION danach noch die Position liefert.
    bool end = it->second.stream.peek() == std::char_traits<char>::eof();
    if (end) it->second.stream.clear();
    return end;
}

void FileSet::closeAll() {
    for (auto& entry : files_) {
        if (entry.second.stream.is_open()) {
            entry.second.stream.flush();
            entry.second.stream.close();
        }
    }
    files_.clear();
}
