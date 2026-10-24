// GameBasiX - Spielpaket
//
// Aufbau der Datei:
//   [gbx.exe][Index][Daten][Fuss]  (Daten = Quellen + INLINE-Bibliothek)
// Der Index nennt jeden Eintrag mit Name und Groesse, die Daten liegen in
// derselben Reihenfolge dahinter. Der Fuss (24 Byte) besteht aus der Kennung
// "GBXPKG01", der Indexgroesse und der Datengroesse. Beim Start liest der
// Interpreter den Fuss, haengt alles in den Temp-Ordner aus und laeuft von
// dort. Assets bleiben draussen; der Export kopiert sie neben die EXE.
#include "package.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "i18n.h"
#include "inline.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[8] = {'G', 'B', 'X', 'P', 'K', 'G', '0', '1'};
constexpr uint64_t kFooterSize = 24;  // 8 Kennung + 8 Indexgroesse + 8 Datengroesse
constexpr uint64_t kMaxPackage = 256ull * 1024 * 1024;
const char* kEntryName = "__gbx_entry";
const char* kInlineDir = "__inline";

// --------------------------------------------------------------- Rohdaten

void putU32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

void putU64(std::string& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

/** Liest Zahlen aus einem Puffer und merkt sich, wenn er zu kurz war. */
struct Reader {
    const std::string& data;
    size_t pos = 0;
    bool ok = true;

    explicit Reader(const std::string& source) : data(source) {}

    uint32_t u32() {
        if (!ok || pos + 4 > data.size()) {
            ok = false;
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(static_cast<unsigned char>(data[pos + i])) << (8 * i);
        pos += 4;
        return value;
    }

    uint64_t u64() {
        if (!ok || pos + 8 > data.size()) {
            ok = false;
            return 0;
        }
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<uint64_t>(static_cast<unsigned char>(data[pos + i])) << (8 * i);
        pos += 8;
        return value;
    }

    std::string bytes(uint64_t count) {
        if (!ok || count > data.size() - pos) {
            ok = false;
            return std::string();
        }
        std::string out = data.substr(pos, static_cast<size_t>(count));
        pos += static_cast<size_t>(count);
        return out;
    }
};

uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hexHash(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

std::string lowerAscii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string libName() {
#ifdef _WIN32
    return std::string(kInlineDir) + "/gbx_inline.dll";
#else
    return std::string(kInlineDir) + "/gbx_inline.so";
#endif
}

std::string exeExtension() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

std::string sanitizedStem(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*') {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    const size_t first = out.find_first_not_of(" .");
    const size_t last = out.find_last_not_of(" .");
    if (first == std::string::npos) return "spiel";
    return out.substr(first, last - first + 1);
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec)) {
        if (fs::equivalent(a, b, ec)) return true;
    }
    std::string left = lowerAscii(a.lexically_normal().generic_string());
    std::string right = lowerAscii(b.lexically_normal().generic_string());
    return left == right;
}

bool readFileText(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

/** Ein Eintrag im Paket: Name relativ zum Projektordner und Groesse. */
struct PackedFile {
    std::string name;
    fs::path path;
    uint64_t size = 0;
};

bool skipDirectory(const std::string& name) {
    const std::string lower = lowerAscii(name);
    static const char* kSkip[] = {".git",  ".svn",        ".hg",   ".vs",      ".vscode",
                                  ".idea", "node_modules", "__pycache__", "build", "build-static",
                                  "dist",  "out",         kInlineDir};
    for (const char* skip : kSkip)
        if (lower == skip) return true;
    return false;
}

/** Quellen kommen ins Paket, alles andere bleibt als Datei neben der EXE. */
bool isSourceFile(const std::string& name) {
    const std::string ext = lowerAscii(fs::path(name).extension().string());
    static const char* kSources[] = {".gbx", ".gbxproj", ".h", ".hpp", ".hh", ".hxx", ".inl"};
    for (const char* source : kSources)
        if (ext == source) return true;
    return false;
}

/**
 * Sammelt den Projektordner. Quellen wandern ins Paket, alle anderen Dateien
 * (Assets) werden spaeter neben die EXE kopiert. Build- und Werkzeugordner
 * bleiben draussen, das Ziel (die alte EXE) ebenfalls.
 */
bool collectFiles(const fs::path& root, const fs::path& target, std::vector<PackedFile>& sources,
                  std::vector<PackedFile>& assets, uint64_t& total, std::string& error) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        error = "Projektordner laesst sich nicht lesen: " + root.string();
        return false;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::directory_entry& entry = *it;
        std::error_code inner;

        if (entry.is_directory(inner)) {
            if (skipDirectory(entry.path().filename().string())) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(inner)) continue;

        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (samePath(entry.path(), target)) continue;

        fs::path relative = fs::relative(entry.path(), root, inner);
        if (inner || relative.empty()) continue;
        const std::string relName = relative.generic_string();
        if (relName.rfind("..", 0) == 0 || relName == kEntryName || relName.find(':') != std::string::npos)
            continue;

        const uint64_t size = entry.file_size(inner);
        if (inner) continue;
        if (!isSourceFile(relName)) {
            assets.push_back(PackedFile{relName, entry.path(), size});
            continue;
        }

        total += size;
        if (total > kMaxPackage) {
            error = "Quellen sind zu gross fuer ein Paket (Grenze 256 MB).";
            return false;
        }
        sources.push_back(PackedFile{relName, entry.path(), size});
    }

    const auto byName = [](const PackedFile& a, const PackedFile& b) { return a.name < b.name; };
    std::sort(sources.begin(), sources.end(), byName);
    std::sort(assets.begin(), assets.end(), byName);
    return true;
}

/** Namen aus dem Paket duerfen nicht aus dem Zielordner ausbrechen. */
bool safeName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos ||
        name.find('\\') != std::string::npos)
        return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t slash = name.find('/', start);
        if (slash == std::string::npos) slash = name.size();
        const std::string part = name.substr(start, slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = slash + 1;
    }
    return true;
}

/** Kopiert einen Bereich aus der Paketdatei in den Zielstrom. */
bool copyRange(std::ifstream& in, uint64_t offset, uint64_t size, std::ostream& out,
               std::string& error) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) {
        error = "Paket laesst sich nicht lesen.";
        return false;
    }
    char buffer[65536];
    uint64_t left = size;
    while (left > 0) {
        const std::streamsize want = static_cast<std::streamsize>(std::min<uint64_t>(left, sizeof(buffer)));
        in.read(buffer, want);
        if (in.gcount() != want) {
            error = "Paket ist beschaedigt (Daten fehlen).";
            return false;
        }
        out.write(buffer, want);
        if (!out) {
            error = "Schreiben fehlgeschlagen.";
            return false;
        }
        left -= static_cast<uint64_t>(want);
    }
    return true;
}

/** Schaltet das Windows-Subsystem von Konsole auf Fenster um: Ein fertiges
 *  Spiel oeffnet so keine Eingabeaufforderung mehr. */
bool patchSubsystem(const fs::path& exe, std::string& error) {
#ifdef _WIN32
    std::fstream file(exe, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        error = "EXE laesst sich nicht oeffnen: " + exe.string();
        return false;
    }
    unsigned char dos[64];
    file.read(reinterpret_cast<char*>(dos), sizeof(dos));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(dos))) {
        error = "EXE ist zu kurz.";
        return false;
    }
    const uint32_t peOffset = static_cast<uint32_t>(dos[0x3C]) |
                              (static_cast<uint32_t>(dos[0x3D]) << 8) |
                              (static_cast<uint32_t>(dos[0x3E]) << 16) |
                              (static_cast<uint32_t>(dos[0x3F]) << 24);
    file.seekg(static_cast<std::streamoff>(peOffset));
    char signature[4] = {};
    file.read(signature, sizeof(signature));
    if (!file || std::memcmp(signature, "PE\0\0", 4) != 0) {
        error = "Keine gueltige Windows-EXE.";
        return false;
    }
    // COFF-Kopf (20 Byte) ueberspringen, dann die Kennung des Optional-Kopfs.
    file.seekg(static_cast<std::streamoff>(peOffset + 4 + 20));
    unsigned char magic[2] = {};
    file.read(reinterpret_cast<char*>(magic), sizeof(magic));
    if (!file) {
        error = "Optional-Kopf fehlt.";
        return false;
    }
    const uint16_t kind = static_cast<uint16_t>(magic[0] | (magic[1] << 8));
    if (kind != 0x10b && kind != 0x20b) {
        error = "Unbekanntes EXE-Format.";
        return false;
    }
    // Das Subsystem steht in beiden Formaten an derselben Stelle (0x44).
    const std::streamoff subsystemAt = static_cast<std::streamoff>(peOffset + 4 + 20 + 0x44);
    file.seekg(subsystemAt);
    unsigned char subsystem[2] = {};
    file.read(reinterpret_cast<char*>(subsystem), sizeof(subsystem));
    if (!file) {
        error = "Subsystem-Feld fehlt.";
        return false;
    }
    if (subsystem[0] == 2 && subsystem[1] == 0) return true;  // schon Fenster
    const unsigned char gui[2] = {2, 0};
    file.seekp(subsystemAt);
    file.write(reinterpret_cast<const char*>(gui), sizeof(gui));
    file.flush();
    if (!file) {
        error = "Subsystem liess sich nicht schreiben.";
        return false;
    }
    return true;
#else
    (void)exe;
    (void)error;
    return true;
#endif
}

}  // namespace

namespace gbxpackage {

std::string executablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::string();
    return std::string(buffer, len);
#else
    char buffer[4096];
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return std::string();
    buffer[len] = '\0';
    return std::string(buffer);
#endif
}

bool selfContained() {
#if defined(_WIN32) && !defined(GBX_STATIC_RUNTIME)
    return false;
#else
    return true;
#endif
}

bool mountSelf(std::string& entryPath, bool& found, std::string& error) {
    found = false;
    const std::string self = executablePath();
    if (self.empty()) return true;

    std::ifstream in(self, std::ios::binary);
    if (!in) return true;
    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < static_cast<std::streamoff>(kFooterSize)) return true;

    in.seekg(static_cast<std::streamoff>(fileSize) - static_cast<std::streamoff>(kFooterSize));
    std::string footer(static_cast<size_t>(kFooterSize), '\0');
    in.read(footer.data(), static_cast<std::streamsize>(footer.size()));
    if (!in || std::memcmp(footer.data(), kMagic, sizeof(kMagic)) != 0) return true;

    found = true;
    Reader reader(footer);
    reader.pos = sizeof(kMagic);
    const uint64_t indexSize = reader.u64();
    const uint64_t dataSize = reader.u64();
    if (!reader.ok || indexSize == 0 || indexSize + dataSize + kFooterSize > static_cast<uint64_t>(fileSize)) {
        error = "Spielpaket ist beschaedigt (Fuss passt nicht zur Datei).";
        return false;
    }

    const uint64_t indexStart = static_cast<uint64_t>(fileSize) - kFooterSize - indexSize - dataSize;
    in.clear();
    in.seekg(static_cast<std::streamoff>(indexStart));
    std::string index(static_cast<size_t>(indexSize), '\0');
    in.read(index.data(), static_cast<std::streamsize>(index.size()));
    if (!in) {
        error = "Spielpaket ist beschaedigt (Index fehlt).";
        return false;
    }

    Reader entries(index);
    const uint32_t count = entries.u32();
    if (!entries.ok || count == 0 || count > 100000) {
        error = "Spielpaket ist beschaedigt (Index unlesbar).";
        return false;
    }

    struct Entry {
        std::string name;
        uint64_t size = 0;
    };
    std::vector<Entry> list;
    list.reserve(count);
    uint64_t expected = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t nameLength = entries.u32();
        if (!entries.ok || nameLength == 0 || nameLength > 4096) {
            error = "Spielpaket ist beschaedigt (Dateiname unlesbar).";
            return false;
        }
        Entry entry;
        entry.name = entries.bytes(nameLength);
        entry.size = entries.u64();
        if (!entries.ok) {
            error = "Spielpaket ist beschaedigt (Eintrag unlesbar).";
            return false;
        }
        expected += entry.size;
        list.push_back(std::move(entry));
    }
    if (expected != dataSize) {
        error = "Spielpaket ist beschaedigt (Groessen passen nicht).";
        return false;
    }

    // Der Cache-Name haengt am Inhalt des Index, an der Dateigroesse und an der
    // Aenderungszeit: ein neuer Export bekommt damit einen frischen Ordner.
    std::error_code ec;
    auto stamp = fs::last_write_time(self, ec);
    const int64_t stampValue = ec ? 0 : static_cast<int64_t>(stamp.time_since_epoch().count());
    std::string key = index;
    key += "|" + std::to_string((unsigned long long)dataSize) + "|" +
           std::to_string((unsigned long long)fileSize) + "|" + std::to_string((long long)stampValue);
    const std::string tag = hexHash(fnv1a(key));

    const fs::path mount = fs::temp_directory_path(ec) / "gbx-run" / tag;
    if (ec) {
        error = "Temp-Ordner nicht verfuegbar.";
        return false;
    }
    const fs::path marker = mount / ".complete";

    bool ready = false;
    {
        std::string text;
        if (readFileText(marker, text)) {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            ready = text == tag;
        }
    }

    if (!ready) {
        std::error_code clearError;
        fs::remove_all(mount, clearError);
        fs::create_directories(mount, clearError);
        if (clearError) {
            error = "Paketordner laesst sich nicht anlegen: " + mount.string();
            return false;
        }

        uint64_t offset = indexStart + indexSize;
        for (const Entry& entry : list) {
            if (!safeName(entry.name)) {
                error = "Ungueltiger Dateiname im Paket: " + entry.name;
                return false;
            }
            const fs::path out = mount / fs::path(entry.name);
            std::error_code dirError;
            if (!out.parent_path().empty()) fs::create_directories(out.parent_path(), dirError);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            if (!file) {
                error = "Datei aus dem Paket laesst sich nicht schreiben: " + out.string();
                return false;
            }
            if (!copyRange(in, offset, entry.size, file, error)) return false;
            offset += entry.size;
        }

        std::ofstream mark(marker, std::ios::binary | std::ios::trunc);
        if (mark) mark << tag << "\n";  // fehlender Marker kostet nur etwas Zeit
    }

    std::string entryName;
    if (!readFileText(mount / kEntryName, entryName)) {
        error = "Spielpaket ohne Einstiegspunkt.";
        return false;
    }
    while (!entryName.empty() && (entryName.back() == '\n' || entryName.back() == '\r' ||
                                  entryName.back() == ' '))
        entryName.pop_back();
    if (entryName.empty()) {
        error = "Spielpaket ohne Einstiegspunkt.";
        return false;
    }

    // Assets liegen neben der EXE, nicht im Paket: Der Programmordner ist der
    // Arbeitsordner, die Quellen kommen aus dem Temp-Ordner.
    const fs::path programDir = fs::path(self).parent_path();
    fs::current_path(programDir, ec);
    if (ec) {
        error = "Wechsel in den Programmordner fehlgeschlagen: " + programDir.string();
        return false;
    }
    entryPath = (mount / fs::path(entryName)).string();
    return true;
}

bool exportProject(const Program& prog, const ProjectConfig& cfg, const std::string& target,
                   std::string& report, std::string& error) {
    std::error_code ec;
    const fs::path self = executablePath();
    if (self.empty()) {
        error = "Die laufende EXE laesst sich nicht bestimmen.";
        return false;
    }

    const fs::path root = fs::absolute(fs::path(cfg.projectDir), ec).lexically_normal();
    if (ec) {
        error = "Projektordner laesst sich nicht bestimmen.";
        return false;
    }
    fs::path outFile = target.empty() ? (root / (sanitizedStem(cfg.name) + exeExtension()))
                                      : fs::absolute(fs::path(target), ec);
    outFile = outFile.lexically_normal();
    if (samePath(outFile, self)) {
        error = "Das Ziel ist die laufende gbx.exe. Bitte einen anderen Dateinamen waehlen.";
        return false;
    }

    // Startet das Projekt ueber eine .gbxproj, kommt sie ins Paket: Nur so
    // bleiben Fenstergroesse, Titel, FPS und Ton erhalten.
    fs::path startFile = fs::absolute(fs::path(prog.mainFile), ec).lexically_normal();
    if (!cfg.projectFile.empty()) {
        const fs::path projectPath = fs::absolute(fs::path(cfg.projectFile), ec).lexically_normal();
        if (lowerAscii(projectPath.extension().string()) == ".gbxproj") startFile = projectPath;
    }
    fs::path entryRelative = fs::relative(startFile, root, ec);
    const std::string entryName = entryRelative.generic_string();
    if (ec || entryName.empty() || entryName.rfind("..", 0) == 0) {
        error = "Die Einstiegsdatei muss im Projektordner liegen.";
        return false;
    }

    std::printf("%s\n", i18n::tr("Sammle Projektdateien ...").c_str());
    std::fflush(stdout);
    std::vector<PackedFile> sources;
    std::vector<PackedFile> assets;
    uint64_t total = 0;
    if (!collectFiles(root, outFile, sources, assets, total, error)) return false;

    // INLINE-Bloecke einmal uebersetzen: Das fertige Spiel soll keinen
    // Compiler mehr brauchen.
    std::vector<std::pair<std::string, std::string>> extras;
    uint64_t inlineSize = 0;
    if (gbxinline_runtime::present(prog)) {
        std::printf("%s\n", i18n::tr("Uebersetze INLINE-Bloecke ...").c_str());
        std::fflush(stdout);
        std::string library;
        try {
            library = gbxinline_runtime::compileForExport(prog);
        } catch (BasicError& e) {
            error = e.fullMessage();
            return false;
        }
        std::string bytes;
        if (!readFileText(library, bytes)) {
            error = "INLINE-Bibliothek laesst sich nicht lesen: " + library;
            return false;
        }
        inlineSize = bytes.size();
        extras.emplace_back(libName(), std::move(bytes));
        extras.emplace_back(std::string(kInlineDir) + "/gbx_inline.sig",
                            gbxinline_runtime::sourceSignature(prog));
    }

    std::string index;
    putU32(index, static_cast<uint32_t>(sources.size() + extras.size() + 1));
    putU32(index, static_cast<uint32_t>(std::strlen(kEntryName)));
    index += kEntryName;
    putU64(index, entryName.size());
    for (const PackedFile& file : sources) {
        putU32(index, static_cast<uint32_t>(file.name.size()));
        index += file.name;
        putU64(index, file.size);
    }
    for (const auto& extra : extras) {
        putU32(index, static_cast<uint32_t>(extra.first.size()));
        index += extra.first;
        putU64(index, extra.second.size());
    }

    uint64_t dataSize = entryName.size();
    for (const PackedFile& file : sources) dataSize += file.size;
    for (const auto& extra : extras) dataSize += extra.second.size();

    if (!outFile.parent_path().empty()) fs::create_directories(outFile.parent_path(), ec);

    std::ifstream base(self, std::ios::binary);
    if (!base) {
        error = "Die laufende EXE laesst sich nicht lesen: " + self.string();
        return false;
    }
    std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Ziel laesst sich nicht schreiben: " + outFile.string();
        return false;
    }
    out << base.rdbuf();
    out.write(index.data(), static_cast<std::streamsize>(index.size()));
    out.write(entryName.data(), static_cast<std::streamsize>(entryName.size()));

    for (const PackedFile& file : sources) {
        std::ifstream source(file.path, std::ios::binary);
        if (!source) {
            error = "Datei laesst sich nicht lesen: " + file.path.string();
            return false;
        }
        out << source.rdbuf();
    }
    for (const auto& extra : extras)
        out.write(extra.second.data(), static_cast<std::streamsize>(extra.second.size()));

    std::string footer(kMagic, sizeof(kMagic));
    putU64(footer, index.size());
    putU64(footer, dataSize);
    out.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    out.flush();
    const bool written = static_cast<bool>(out);
    out.close();
    base.close();
    if (!written) {
        error = "Schreiben fehlgeschlagen: " + outFile.string();
        return false;
    }

    std::string patchError;
    if (!patchSubsystem(outFile, patchError)) {
        std::printf("%s\n", i18n::tr("Hinweis: " + patchError).c_str());
    }

    // Assets bleiben bewusst ausserhalb der EXE: Sie werden neben das Spiel
    // kopiert und lassen sich dort austauschen, ohne neu zu packen.
    size_t copied = 0;
    for (const PackedFile& asset : assets) {
        const fs::path dest = outFile.parent_path() / fs::path(asset.name);
        if (samePath(dest, asset.path)) continue;
        std::error_code copyError;
        if (!dest.parent_path().empty()) fs::create_directories(dest.parent_path(), copyError);
        fs::copy_file(asset.path, dest, fs::copy_options::overwrite_existing, copyError);
        if (copyError) {
            error = "Asset laesst sich nicht kopieren: " + asset.path.string();
            return false;
        }
        ++copied;
    }

    std::ostringstream text;
    text << i18n::tr("EXE erstellt: ") << outFile.string() << "\n";
    text << "  " << sources.size() << i18n::tr(" Quelldateien im Paket, ")
         << (dataSize / 1024) << i18n::tr(" KB");
    if (copied > 0) text << i18n::tr(", ") << copied << i18n::tr(" Assets daneben kopiert");
    if (inlineSize > 0) text << i18n::tr(", INLINE-Bibliothek eingebettet");
    text << "\n";
    if (!selfContained()) {
        text << i18n::tr("  Hinweis: Dieser Interpreter braucht C++-Laufzeit-DLLs. Fuer eine "
                         "EXE ohne fremde DLLs mit -DGBX_STATIC_RUNTIME=ON bauen.\n");
    }
    report = text.str();
    return true;
}

}  // namespace gbxpackage
