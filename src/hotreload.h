// GameBasiX - Assets im laufenden Programm nachladen (Hot Reload)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Runtime;

// Ein Asset, das beim Start geladen wurde. Aendert sich die Datei auf der
// Platte, wird es unter derselben Nummer ersetzt: das Spiel sieht das neue
// Bild, die neue Schrift oder den neuen Klang, ohne neu zu starten.
struct AssetWatch {
    enum class Kind { Sprite, Background, Font, Sound, Music };
    Kind kind = Kind::Sprite;
    std::string path;    // aufgeloester Pfad, wie ihn der Lader benutzt hat
    std::string label;   // wie im Programm geschrieben, nur fuer Meldungen
    int id = 0;          // Sprite-, Font- oder Sound-Nummer
    int points = 0;      // TTF-Groesse, 0 = Bitmap-Schrift
    int buffers = 1;     // SOUND_LOAD
    bool loop = true;    // MUSIC_PLAY
    uint64_t stamp = 0;  // Groesse und Zeitstempel beim letzten Laden
    bool here = true;    // Datei war beim letzten Versuch vorhanden
};

class HotReload {
public:
    // Ein Durchlauf hoechstens alle 200 ms und hoechstens 64 Dateien, damit
    // ein Spiel mit vielen Bildern nicht jedes Bild alle Dateien anfasst.
    static constexpr uint64_t kIntervalMs = 200;
    static constexpr size_t kPerTick = 64;

    // Merkt sich ein geladenes Asset. Gleiche Art und Nummer ersetzen den
    // alten Eintrag - ein zweites SPRITE_LOAD auf dieselbe Nummer gewinnt.
    void watch(const AssetWatch& entry);
    void forget();

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }
    size_t count() const { return entries_.size(); }

    // Wird einmal pro Bild von Gfx::present aufgerufen.
    void tick(Runtime& rt);
    // Laedt alle beobachteten Assets sofort neu (ASSET_RELOAD).
    int reloadAll(Runtime& rt);

    // Groesse und Zeitstempel einer Datei in einem Wert; 0 = nicht lesbar.
    static uint64_t stampOf(const std::string& path);
    static const char* kindName(AssetWatch::Kind kind);

private:
    bool reload(Runtime& rt, AssetWatch& entry);
    void note(const std::string& text) const;

    std::vector<AssetWatch> entries_;
    size_t cursor_ = 0;
    uint64_t lastTick_ = 0;
    bool enabled_ = true;
};
