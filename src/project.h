// GameBasiX - Projektdatei (.gbxproj, INI-Format)
#pragma once

#include <string>
#include <vector>

struct ProjectConfig {
    std::string name = "GBX Programm";
    std::string entry;               // Quelldatei relativ zum Projektverzeichnis
    std::string projectFile;
    std::string projectDir = ".";

    std::string title = "GameBasiX";
    int width = 960;
    int height = 600;
    bool fullscreen = false;
    bool resizable = true;

    double fps = 60.0;
    bool vsync = true;

    bool audio = true;
    double musicVolume = 1.0;

    std::string assetDir = "assets";

    // Sektion [native]: C++-Bibliotheken fuer die INLINE-Bloecke. Ein Archiv
    // (.a) oder eine Objektdatei (.o) wird beim Uebersetzen mitgelinkt; dazu
    // Suchordner (-L) und weitere Flags. Relative Angaben gelten ab dem
    // Projektordner. Siehe README, Abschnitt "Native libraries".
    std::vector<std::string> nativeLibs;
    std::vector<std::string> nativeLibDirs;
    std::vector<std::string> nativeFlags;
};

// Liest eine .gbxproj-Datei. Wirft BasicError bei Fehlern.
ProjectConfig loadProject(const std::string& path);

// Ermittelt aus einem beliebigen Startargument eine Konfiguration: bei einer
// .gbxproj wird sie gelesen, bei einer Quelldatei werden Standardwerte benutzt.
ProjectConfig configForEntry(const std::string& path);
