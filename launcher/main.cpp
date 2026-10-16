// Launcher del juego: comprueba actualizaciones, las descarga/aplica,
// gestiona mods (activar/desactivar) y lanza el ejecutable del juego.
//
// Compilar (consola "x64 Native Tools Command Prompt for VS"):
//   rc app.rc
//   cl /EHsc /std:c++17 /DUNICODE /D_UNICODE /O2 main.cpp app.res /Fe:Launcher.exe ^
//      /link user32.lib gdi32.lib comctl32.lib uxtheme.lib msimg32.lib winhttp.lib shell32.lib

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <filesystem>
#include <functional>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:windows")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// ============================================================= Configuración
// Cambia esta URL por la de tu propio repositorio (archivo version.json "raw").
static std::wstring MANIFEST_URL =
    L"https://raw.githubusercontent.com/TU_USUARIO/TU_REPO/main/version.json";
static const std::wstring GAME_DIR      = L"game";
static const std::wstring GAME_EXE      = L"game\\Game.exe";
static const std::wstring MODS_DIR      = L"game\\mods";
static const std::wstring VERSION_FILE  = L"version.txt";
static const std::wstring CONFIG_FILE   = L"launcher_config.ini";

// ============================================================= Colores / UI
static const COLORREF C_BG     = RGB(16, 16, 20);
static const COLORREF C_SIDE   = RGB(10, 10, 13);
static const COLORREF C_PANEL  = RGB(26, 26, 32);
static const COLORREF C_ACCENT = RGB(88, 166, 255);
static const COLORREF C_GOOD   = RGB(90, 200, 120);
static const COLORREF C_TEXT   = RGB(230, 230, 235);
static const COLORREF C_MUTED  = RGB(140, 140, 150);

enum { ID_NAV_HOME = 101, ID_NAV_MODS, ID_NAV_SETTINGS,
       ID_CHECK = 201, ID_PLAY, ID_PROGRESS, ID_MODLIST, ID_MOD_TOGGLE, ID_OPEN_MODS,
       ID_CFG_URL, ID_CFG_SAVE };
enum Page { PAGE_HOME, PAGE_MODS, PAGE_SETTINGS };

#define WM_APP_LOG       (WM_APP + 1)
#define WM_APP_PROGRESS  (WM_APP + 2)
#define WM_APP_DONE      (WM_APP + 3)
#define WM_APP_UPDATE_FOUND (WM_APP + 4)

static HWND g_hMain;
static HWND g_hStatus, g_hVersion, g_hProgress, g_hCheck, g_hPlay, g_hModList;
static HWND g_hUrlEdit;
static HFONT g_fNormal, g_fSmall, g_fTitle, g_fBtn;
static HBRUSH g_bgBrush;
static Page g_page = PAGE_HOME;
static std::vector<HWND> g_pages[3];
static std::atomic<bool> g_busy(false);
static std::wstring g_remoteVersion, g_remoteUrl, g_remoteNotes;

// ============================================================= Utilidades de archivo
static std::wstring ReadFileAll(const std::wstring& path) {
    std::wifstream f(path);
    if (!f) return L"";
    std::wstringstream ss; ss << f.rdbuf();
    return ss.str();
}
static void WriteFileAll(const std::wstring& path, const std::wstring& content) {
    std::wofstream f(path, std::ios::trunc);
    f << content;
}
static std::wstring GetLocalVersion() {
    std::wstring v = ReadFileAll(VERSION_FILE);
    while (!v.empty() && (v.back() == L'\n' || v.back() == L'\r')) v.pop_back();
    return v.empty() ? L"0.0.0" : v;
}

// ============================================================= Mini-parser JSON (manifest simple y fijo)
// Espera un objeto plano: {"version":"1.0.1","url":"https://...zip","notes":"texto"}
static std::wstring JsonGetString(const std::wstring& json, const std::wstring& key) {
    std::wstring pat = L"\"" + key + L"\"";
    size_t p = json.find(pat);
    if (p == std::wstring::npos) return L"";
    p = json.find(L':', p);
    if (p == std::wstring::npos) return L"";
    p = json.find(L'"', p);
    if (p == std::wstring::npos) return L"";
    size_t end = p + 1;
    std::wstring out;
    while (end < json.size() && json[end] != L'"') {
        if (json[end] == L'\\' && end + 1 < json.size()) { out += json[end + 1]; end += 2; continue; }
        out += json[end]; ++end;
    }
    return out;
}

// ============================================================= HTTP (WinHTTP)
static bool HttpGetText(const std::wstring& url, std::wstring& outText) {
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSession = WinHttpOpen(L"GameLauncher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : nullptr;
    bool ok = false;
    if (hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hRequest, nullptr)) {
        std::string raw;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            raw.append(buf.data(), read);
        }
        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
        outText.assign(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), outText.data(), wlen);
        ok = !raw.empty();
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

static bool HttpDownloadFile(const std::wstring& url, const std::wstring& outPath,
                              const std::function<void(int)>& onProgress) {
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSession = WinHttpOpen(L"GameLauncher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : nullptr;
    bool ok = false;
    if (hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD total = 0, sz = sizeof(total);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz, WINHTTP_NO_HEADER_INDEX);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        DWORD avail = 0, received = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            out.write(buf.data(), read);
            received += read;
            if (total > 0 && onProgress) onProgress((int)(received * 100.0 / total));
        }
        out.close();
        ok = received > 0;
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// ============================================================= Extraer ZIP (usa PowerShell, incluido en Windows)
static bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    fs::create_directories(destDir);
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        L"\"Expand-Archive -LiteralPath '" + zipPath + L"' -DestinationPath '" + destDir + L"' -Force\"";
    STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code == 0;
}

// ============================================================= Lógica de actualización (hilo aparte)
static void PostLog(const std::wstring& s) {
    auto* p = new std::wstring(s);
    PostMessage(g_hMain, WM_APP_LOG, 0, (LPARAM)p);
}

static void CheckForUpdatesAsync() {
    if (g_busy) return;
    g_busy = true;
    std::thread([]() {
        PostLog(L"Buscando actualizaciones...");
        std::wstring json;
        if (!HttpGetText(MANIFEST_URL, json)) {
            PostLog(L"No se pudo contactar al servidor de actualizaciones.");
            PostMessage(g_hMain, WM_APP_DONE, 0, 0);
            return;
        }
        g_remoteVersion = JsonGetString(json, L"version");
        g_remoteUrl     = JsonGetString(json, L"url");
        g_remoteNotes   = JsonGetString(json, L"notes");
        std::wstring local = GetLocalVersion();

        if (g_remoteVersion.empty()) {
            PostLog(L"Respuesta del servidor inválida.");
        } else if (g_remoteVersion == local) {
            PostLog(L"Ya tienes la última versión (" + local + L").");
        } else {
            PostMessage(g_hMain, WM_APP_UPDATE_FOUND, 0, 0);
            return;
        }
        PostMessage(g_hMain, WM_APP_DONE, 0, 0);
    }).detach();
}

static void DownloadAndApplyUpdate() {
    if (g_busy) return;
    g_busy = true;
    std::thread([]() {
        PostLog(L"Descargando versión " + g_remoteVersion + L"...");
        std::wstring zipPath = L"update_" + g_remoteVersion + L".zip";
        bool ok = HttpDownloadFile(g_remoteUrl, zipPath, [](int pct) {
            PostMessage(g_hMain, WM_APP_PROGRESS, pct, 0);
        });
        if (!ok) {
            PostLog(L"Falló la descarga de la actualización.");
            PostMessage(g_hMain, WM_APP_DONE, 0, 0);
            return;
        }
        PostLog(L"Aplicando actualización...");
        if (!ExtractZip(zipPath, GAME_DIR)) {
            PostLog(L"Falló al extraer la actualización.");
            PostMessage(g_hMain, WM_APP_DONE, 0, 0);
            return;
        }
        DeleteFileW(zipPath.c_str());
        WriteFileAll(VERSION_FILE, g_remoteVersion);
        PostLog(L"¡Actualizado a la versión " + g_remoteVersion + L"!");
        PostMessage(g_hMain, WM_APP_DONE, 0, 0);
    }).detach();
}

// ============================================================= Mods
static void RefreshModList() {
    SendMessage(g_hModList, LB_RESETCONTENT, 0, 0);
    if (!fs::exists(MODS_DIR)) { fs::create_directories(MODS_DIR); return; }
    for (auto& entry : fs::directory_iterator(MODS_DIR)) {
        if (!entry.is_regular_file()) continue;
        std::wstring name = entry.path().filename().wstring();
        bool disabled = name.size() > 9 && name.substr(name.size() - 9) == L".disabled";
        std::wstring label = (disabled ? L"[Desactivado] " : L"[Activado] ") +
                              (disabled ? name.substr(0, name.size() - 9) : name);
        SendMessage(g_hModList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
}

static void ToggleSelectedMod() {
    int sel = (int)SendMessage(g_hModList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    int i = 0;
    for (auto& entry : fs::directory_iterator(MODS_DIR)) {
        if (!entry.is_regular_file()) continue;
        if (i++ != sel) continue;
        std::wstring p = entry.path().wstring();
        if (p.size() > 9 && p.substr(p.size() - 9) == L".disabled")
            fs::rename(p, p.substr(0, p.size() - 9));
        else
            fs::rename(p, p + L".disabled");
        break;
    }
    RefreshModList();
}

// ============================================================= Lanzar el juego
static void LaunchGame() {
    if (!fs::exists(GAME_EXE)) {
        MessageBoxW(g_hMain, L"No se encontró el juego. Exporta el proyecto de Godot a game\\Game.exe",
                   L"Juego no encontrado", MB_OK | MB_ICONWARNING);
        return;
    }
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + fs::absolute(GAME_EXE).wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       fs::absolute(GAME_DIR).wstring().c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

// ============================================================= Construcción UI (resumido: una sola página visible)
static HFONT MakeFont(int h, int weight) {
    return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
static HWND Make(Page pg, const wchar_t* cls, const wchar_t* text, DWORD style,
                 int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style, x, y, w, h, g_hMain,
                             (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessage(c, WM_SETFONT, (WPARAM)font, TRUE);
    g_pages[pg].push_back(c);
    return c;
}
static void ShowPage(Page p) {
    g_page = p;
    for (int i = 0; i < 3; ++i) for (HWND h : g_pages[i]) ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
    for (int id = ID_NAV_HOME; id <= ID_NAV_SETTINGS; ++id) InvalidateRect(GetDlgItem(g_hMain, id), nullptr, TRUE);
}

static void BuildUI(HWND hwnd) {
    g_hMain = hwnd;
    g_fNormal = MakeFont(16, FW_NORMAL);
    g_fSmall  = MakeFont(14, FW_NORMAL);
    g_fTitle  = MakeFont(24, FW_BOLD);
    g_fBtn    = MakeFont(16, FW_BOLD);
    g_bgBrush = CreateSolidBrush(C_BG);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    const wchar_t* nav[] = { L"INICIO", L"MODS", L"CONFIGURACIÓN" };
    for (int i = 0; i < 3; ++i) {
        HWND b = CreateWindowExW(0, L"BUTTON", nav[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 0, 100 + i * 46, 180, 46, hwnd, (HMENU)(INT_PTR)(ID_NAV_HOME + i),
                                 GetModuleHandle(nullptr), nullptr);
        SendMessage(b, WM_SETFONT, (WPARAM)g_fNormal, TRUE);
    }

    // --- INICIO
    g_hStatus = Make(PAGE_HOME, L"STATIC", L"Listo.", WS_VISIBLE | SS_LEFT, 200, 110, 680, 24, 0, g_fNormal);
    g_hVersion = Make(PAGE_HOME, L"STATIC", (L"Versión instalada: " + GetLocalVersion()).c_str(),
                      WS_VISIBLE | SS_LEFT, 200, 140, 680, 22, 0, g_fSmall);
    g_hProgress = Make(PAGE_HOME, PROGRESS_CLASS, L"", WS_VISIBLE | PBS_SMOOTH, 200, 175, 680, 14, ID_PROGRESS, g_fSmall);
    SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(g_hProgress, PBM_SETBARCOLOR, 0, C_ACCENT);
    g_hCheck = Make(PAGE_HOME, L"BUTTON", L"BUSCAR ACTUALIZACIONES", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    200, 230, 260, 46, ID_CHECK, g_fBtn);
    g_hPlay = Make(PAGE_HOME, L"BUTTON", L"JUGAR", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                   740, 230, 140, 46, ID_PLAY, g_fBtn);

    // --- MODS
    Make(PAGE_MODS, L"STATIC", L"MODS INSTALADOS", WS_VISIBLE, 200, 20, 400, 32, 0, g_fTitle);
    g_hModList = Make(PAGE_MODS, L"LISTBOX", L"", WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER,
                      200, 64, 680, 320, ID_MODLIST, g_fNormal);
    Make(PAGE_MODS, L"BUTTON", L"ACTIVAR / DESACTIVAR", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        200, 396, 220, 42, ID_MOD_TOGGLE, g_fBtn);
    Make(PAGE_MODS, L"BUTTON", L"ABRIR CARPETA DE MODS", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        432, 396, 230, 42, ID_OPEN_MODS, g_fBtn);

    // --- CONFIGURACION
    Make(PAGE_SETTINGS, L"STATIC", L"CONFIGURACIÓN", WS_VISIBLE, 200, 20, 400, 32, 0, g_fTitle);
    Make(PAGE_SETTINGS, L"STATIC", L"URL del manifiesto de actualizaciones:", WS_VISIBLE, 200, 70, 400, 22, 0, g_fSmall);
    g_hUrlEdit = Make(PAGE_SETTINGS, L"EDIT", MANIFEST_URL.c_str(),
                      WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 200, 96, 680, 26, ID_CFG_URL, g_fSmall);
    Make(PAGE_SETTINGS, L"BUTTON", L"GUARDAR", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        200, 136, 140, 38, ID_CFG_SAVE, g_fBtn);

    ShowPage(PAGE_HOME);
    RefreshModList();
}

// ============================================================= Dibujo
static void DrawButton(const DRAWITEMSTRUCT* d) {
    wchar_t text[64]; GetWindowTextW(d->hwndItem, text, 64);
    bool nav = d->CtlID >= ID_NAV_HOME && d->CtlID <= ID_NAV_SETTINGS;
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    HDC dc = d->hDC; RECT r = d->rcItem;
    if (nav) {
        bool sel = (int)(d->CtlID - ID_NAV_HOME) == (int)g_page;
        HBRUSH b = CreateSolidBrush(sel ? C_PANEL : C_SIDE); FillRect(dc, &r, b); DeleteObject(b);
        if (sel) { RECT bar = { 0, r.top, 4, r.bottom }; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &bar, a); DeleteObject(a); }
        SetTextColor(dc, sel ? RGB(255, 255, 255) : C_MUTED);
        r.left += 20; SelectObject(dc, g_fNormal); SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF fill = disabled ? RGB(60, 60, 65) : ((d->itemState & ODS_SELECTED) ? RGB(60, 130, 220) : C_ACCENT);
        HBRUSH b = CreateSolidBrush(fill); FillRect(dc, &r, b); DeleteObject(b);
        SetTextColor(dc, disabled ? RGB(140, 140, 140) : RGB(10, 10, 15));
        SelectObject(dc, g_fBtn); SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ============================================================= WndProc
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: BuildUI(hwnd); return 0;
    case WM_ERASEBKGND: { RECT r; GetClientRect(hwnd, &r); FillRect((HDC)w, &r, g_bgBrush); return 1; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w; SetTextColor(dc, C_TEXT); SetBkColor(dc, (m == WM_CTLCOLORSTATIC) ? C_BG : C_PANEL);
        static HBRUSH pb = CreateSolidBrush(C_PANEL);
        return (INT_PTR)((m == WM_CTLCOLORSTATIC) ? g_bgBrush : pb);
    }
    case WM_DRAWITEM: DrawButton((const DRAWITEMSTRUCT*)l); return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_NAV_HOME) ShowPage(PAGE_HOME);
        else if (id == ID_NAV_MODS) { RefreshModList(); ShowPage(PAGE_MODS); }
        else if (id == ID_NAV_SETTINGS) ShowPage(PAGE_SETTINGS);
        else if (id == ID_CHECK) CheckForUpdatesAsync();
        else if (id == ID_PLAY) LaunchGame();
        else if (id == ID_MOD_TOGGLE) ToggleSelectedMod();
        else if (id == ID_OPEN_MODS) {
            fs::create_directories(MODS_DIR);
            ShellExecuteW(nullptr, L"open", fs::absolute(MODS_DIR).wstring().c_str(), nullptr, nullptr, SW_SHOW);
        } else if (id == ID_CFG_SAVE) {
            wchar_t buf[1024]; GetWindowTextW(g_hUrlEdit, buf, 1024);
            MANIFEST_URL = buf;
            WriteFileAll(CONFIG_FILE, MANIFEST_URL);
            MessageBoxW(hwnd, L"Configuración guardada.", L"Launcher", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }
    case WM_APP_LOG: {
        auto* p = (std::wstring*)l;
        SetWindowTextW(g_hStatus, p->c_str());
        delete p;
        return 0;
    }
    case WM_APP_PROGRESS:
        SendMessage(g_hProgress, PBM_SETPOS, w, 0);
        return 0;
    case WM_APP_UPDATE_FOUND: {
        std::wstring msg = L"Hay una nueva versión disponible: " + g_remoteVersion +
                           L"\n\n" + g_remoteNotes + L"\n\n¿Descargar e instalar ahora?";
        if (MessageBoxW(hwnd, msg.c_str(), L"Actualización disponible", MB_YESNO | MB_ICONQUESTION) == IDYES)
            DownloadAndApplyUpdate();
        else
            g_busy = false;
        return 0;
    }
    case WM_APP_DONE:
        g_busy = false;
        SetWindowTextW(g_hVersion, (L"Versión instalada: " + GetLocalVersion()).c_str());
        SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    std::wstring savedUrl = ReadFileAll(CONFIG_FILE);
    if (!savedUrl.empty()) MANIFEST_URL = savedUrl;
    fs::create_directories(MODS_DIR);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WindowProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GameLauncherClass";
    wc.hIcon = LoadIconW(hInst, L"IDI_ICON1"); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, 920, 500 }; AdjustWindowRect(&rc, style, FALSE);
    int Wd = rc.right - rc.left, H = rc.bottom - rc.top;
    int X = (GetSystemMetrics(SM_CXSCREEN) - Wd) / 2, Y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
    HWND hwnd = CreateWindowExW(0, L"GameLauncherClass", L"Launcher", style, X, Y, Wd, H,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow); UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
