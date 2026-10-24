// GameBasiX - Projektdatei lesen
#include "project.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common.h"

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

bool toBool(const std::string& v) {
    std::string u = upper(trim(v));
    if (u == "1" || u == "TRUE" || u == "YES" || u == "ON" || u == "JA") return true;
    if (u == "0" || u == "FALSE" || u == "NO" || u == "OFF" || u == "NEIN") return false;
    return !v.empty() && v != "0";
}

}  // namespace

ProjectConfig loadProject(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BasicError("Projektdatei nicht gefunden: " + path, SrcPos{path, 0}, true);

    ProjectConfig cfg;
    cfg.projectFile = path;
    fs::path p(path);
    cfg.projectDir = p.parent_path().empty() ? "." : p.parent_path().string();
    cfg.entry = "main.gbx";

    std::string section;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[') {
            size_t close = t.find(']');
            if (close == std::string::npos)
                throw BasicError("Ungueltige Sektion in Projektdatei", SrcPos{path, lineNo}, true);
            section = upper(trim(t.substr(1, close - 1)));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos)
            throw BasicError("Zeile ohne '=' in Projektdatei: " + t, SrcPos{path, lineNo}, true);
        std::string key = upper(trim(t.substr(0, eq)));
        std::string val = unquote(trim(t.substr(eq + 1)));

        if (section == "PROJECT" || section.empty()) {
            if (key == "NAME") cfg.name = val;
            else if (key == "MAIN" || key == "ENTRY") cfg.entry = val;
        } else if (section == "WINDOW") {
            if (key == "TITLE") cfg.title = val;
            else if (key == "WIDTH") cfg.width = std::max(64, std::atoi(val.c_str()));
            else if (key == "HEIGHT") cfg.height = std::max(64, std::atoi(val.c_str()));
            else if (key == "FULLSCREEN") cfg.fullscreen = toBool(val);
            else if (key == "RESIZABLE") cfg.resizable = toBool(val);
        } else if (section == "GRAPHICS") {
            if (key == "FPS") cfg.fps = std::atof(val.c_str());
            else if (key == "VSYNC") cfg.vsync = toBool(val);
        } else if (section == "AUDIO") {
            if (key == "ENABLED") cfg.audio = toBool(val);
            else if (key == "VOLUME") cfg.musicVolume = std::atof(val.c_str());
        } else if (section == "ASSETS" || section == "PATHS") {
            if (key == "ROOT" || key == "DIR") cfg.assetDir = val;
        }
    }

    if (cfg.title.empty()) cfg.title = cfg.name;
    if (cfg.fps <= 0) cfg.fps = 0;  // 0 = unbegrenzt
    return cfg;
}

ProjectConfig configForEntry(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gbxproj") return loadProject(path);

    ProjectConfig cfg;
    cfg.entry = fs::path(path).filename().string();
    cfg.projectDir = fs::path(path).parent_path().empty() ? "." : fs::path(path).parent_path().string();
    cfg.projectFile = path;
    cfg.name = fs::path(path).stem().string();
    cfg.title = cfg.name;
    cfg.assetDir = ".";
    return cfg;
}
