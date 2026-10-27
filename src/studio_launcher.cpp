// GBX Studio - kleines Startprogramm
//
// GBXStudio.exe      startet das Electron-Fenster der IDE aus dem Ordner "ide".
//                    Ist Electron nicht installiert, laeuft die Web-Variante im
//                    Standardbrowser.
// GBXStudio_web.exe  startet immer die Web-Variante: lokaler Server im eigenen
//                    Konsolenfenster, die Oberflaeche im Standardbrowser - auch
//                    wenn Electron daneben installiert ist.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace {

// Beide EXEn entstehen aus dieser Datei; GBX_STUDIO_WEB=1 macht daraus die
// Browser-Variante (siehe CMakeLists.txt).
#ifdef GBX_STUDIO_WEB
constexpr bool kWebOnly = true;
constexpr const wchar_t* kTitle = L"GBX Studio (Web)";
constexpr const wchar_t* kExe = L"GBXStudio_web.exe";
constexpr const wchar_t* kHint =
    L"Danach oeffnet GBXStudio_web.exe die Oberflaeche im Standardbrowser.";
#else
constexpr bool kWebOnly = false;
constexpr const wchar_t* kTitle = L"GBX Studio";
constexpr const wchar_t* kExe = L"GBXStudio.exe";
constexpr const wchar_t* kHint =
    L"Danach oeffnet GBXStudio.exe den Editor im eigenen Fenster "
    L"(ohne Electron im Browser).";
#endif

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, len);
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring(L".") : path.substr(0, cut);
}

bool fileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring quoted(const std::wstring& text) { return L"\"" + text + L"\""; }

// Startet ein Programm und kehrt sofort zurueck; die EXE bleibt nicht im
// Hintergrund. flags steuern, ob ein Konsolenfenster erscheint.
bool launch(const std::wstring& program, const std::wstring& args,
            const std::wstring& workDir, DWORD flags) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring commandLine = quoted(program) + L" " + args;
    std::wstring mutableLine = commandLine;   // CreateProcessW braucht Puffer

    BOOL ok = CreateProcessW(program.c_str(), mutableLine.data(), nullptr, nullptr,
                             FALSE, flags, nullptr, workDir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void fail(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_ICONERROR | MB_OK);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring root = exeDir();
    const std::wstring ide = root + L"\\ide";

    if (!dirExists(ide) || !fileExists(ide + L"\\package.json")) {
        fail(L"Der Ordner \"ide\" mit der IDE wurde neben " + std::wstring(kExe) +
             L" nicht gefunden.\n\nErwartet: " + ide);
        return 1;
    }

    // Eigenes Fenster, wenn Electron da ist - die Web-Variante ueberspringt das.
    const std::wstring electron = ide + L"\\node_modules\\electron\\dist\\electron.exe";
    if (!kWebOnly && fileExists(electron)) {
        if (launch(electron, quoted(ide), ide, CREATE_NO_WINDOW)) return 0;
        fail(L"Electron ließ sich nicht starten:\n" + electron);
        return 1;
    }

    // Webserver starten, der den Browser selbst oeffnet.
    wchar_t nodePath[MAX_PATH] = L"";
    const bool hasNode =
        SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodePath, nullptr) > 0;
    const std::wstring cli = ide + L"\\src\\server\\cli.mjs";
    if (hasNode && fileExists(cli)) {
        if (launch(nodePath, quoted(cli), ide, CREATE_NEW_CONSOLE)) return 0;
    }

    if (!hasNode) {
        fail(L"Node.js wurde nicht gefunden.\n\n"
             L"GBX Studio braucht Node.js (https://nodejs.org) und einmalig im "
             L"Ordner \"ide\":\n    npm install");
        return 1;
    }

    fail(L"GBX Studio ist nicht startklar.\n\n"
         L"Einmalig im Ordner \"ide\" ausführen:\n    npm install\n\n" +
         std::wstring(kHint));
    return 1;
}
