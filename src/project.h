// GameBasiX - Projektdatei (.gbxproj, INI-Format)
#pragma once

#include <string>

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
};

// Liest eine .gbxproj-Datei. Wirft BasicError bei Fehlern.
ProjectConfig loadProject(const std::string& path);

// Ermittelt aus einem beliebigen Startargument eine Konfiguration: bei einer
// .gbxproj wird sie gelesen, bei einer Quelldatei werden Standardwerte benutzt.
ProjectConfig configForEntry(const std::string& path);
