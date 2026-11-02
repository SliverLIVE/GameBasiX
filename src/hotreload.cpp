// GameBasiX - Assets im laufenden Programm nachladen
#include "hotreload.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "runtime.h"

namespace fs = std::filesystem;

const char* HotReload::kindName(AssetWatch::Kind kind) {
    switch (kind) {
        case AssetWatch::Kind::Sprite: return "graphics";
        case AssetWatch::Kind::Background: return "background";
        case AssetWatch::Kind::Font: return "font";
        case AssetWatch::Kind::Sound: return "sound";
        case AssetWatch::Kind::Music: return "music";
    }
    return "Asset";
}

uint64_t HotReload::stampOf(const std::string& path) {
    if (path.empty()) return 0;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) return 0;
    std::error_code wec;
    const auto when = fs::last_write_time(path, wec);
    if (wec) return 0;
    // Groesse und Zeitstempel zusammen: ein Tausch der Datei mit gleicher
    // Groesse faellt damit genauso auf wie ein einzelnes geaendertes Byte.
    const uint64_t ticks = static_cast<uint64_t>(when.time_since_epoch().count());
    return (ticks * 1099511628211ull) ^ (static_cast<uint64_t>(size) << 20);
}

void HotReload::watch(const AssetWatch& entry) {
    if (entry.path.empty()) return;
    AssetWatch copy = entry;
    copy.stamp = stampOf(copy.path);
    copy.here = copy.stamp != 0;
    for (auto& existing : entries_) {
        if (existing.kind == copy.kind && existing.id == copy.id) {
            existing = copy;
            return;
        }
    }
    entries_.push_back(std::move(copy));
}

void HotReload::forget() {
    entries_.clear();
    cursor_ = 0;
    lastTick_ = 0;
}

void HotReload::note(const std::string& text) const {
    // Dieselbe Senke wie DEBUG: das Konsolenfenster der IDE zeigt es an.
    std::fputs(text.c_str(), stderr);
    std::fflush(stderr);
}

bool HotReload::reload(Runtime& rt, AssetWatch& entry) {
    std::string err;
    bool ok = false;
    switch (entry.kind) {
        case AssetWatch::Kind::Sprite:
            ok = rt.gfx.reloadSprite(entry.path, entry.id, err);
            break;
        case AssetWatch::Kind::Background:
            ok = rt.gfx.loadBackground(entry.path, err);
            break;
        case AssetWatch::Kind::Font:
            ok = rt.gfx.reloadFont(entry.id, entry.path, entry.points, err);
            break;
        case AssetWatch::Kind::Sound:
            ok = rt.audio.loadSound(entry.path, entry.id, entry.buffers, err);
            break;
        case AssetWatch::Kind::Music:
            ok = rt.audio.playMusic(entry.path, entry.loop, err);
            break;
    }
    if (!ok) {
        // Eine halb geschriebene Datei ist kein Grund aufzugeben: der
        // naechste Durchlauf versucht es noch einmal.
        note("Hot Reload: " + entry.label + " could not be reloaded (" + err + ")\n");
        return false;
    }
    note(std::string("Hot Reload: ") + kindName(entry.kind) + " " + entry.label + " reloaded\n");
    entry.stamp = stampOf(entry.path);
    entry.here = true;
    return true;
}

void HotReload::tick(Runtime& rt) {
    if (!enabled_ || entries_.empty() || rt.testsMode) return;
    const uint64_t now = SDL_GetTicks();
    if (lastTick_ != 0 && now - lastTick_ < kIntervalMs) return;
    lastTick_ = now;

    const size_t total = entries_.size();
    const size_t count = std::min(total, kPerTick);
    for (size_t i = 0; i < count; ++i) {
        AssetWatch& entry = entries_[(cursor_ + i) % total];
        const uint64_t stamp = stampOf(entry.path);
        if (stamp == 0) {
            // Gerade nicht lesbar (Editor speichert, Datei verschoben): der
            // naechste Durchlauf schaut wieder hin.
            entry.here = false;
            continue;
        }
        if (stamp == entry.stamp && entry.here) continue;
        reload(rt, entry);
    }
    cursor_ = (cursor_ + count) % total;
}

int HotReload::reloadAll(Runtime& rt) {
    int loaded = 0;
    for (auto& entry : entries_) {
        if (reload(rt, entry)) ++loaded;
    }
    return loaded;
}
