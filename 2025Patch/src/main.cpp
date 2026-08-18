#include "RR/Patching/Patches.h"

// Find this process's main game window (top-level, visible, has a title). Used to hand focus back
// after AllocConsole -- injection happens well after Unity's window exists (GameAssembly + Referee
// are already loaded), so it's reliably present here.
static HWND g_gameWindow = nullptr;
static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        char title[128] = {};
        if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            g_gameWindow = hwnd;
            return FALSE; // stop
        }
    }
    return TRUE;
}

// The console is OFF by default -- set EnableConsole=true in 2025patch.ini only for live poking,
// and expect it to cost you load time. Rationale below.
void CreateConsole() {
    // AllocConsole creates a console window that grabs the foreground, and Unity throttles the game
    // to a crawl the instant its window loses focus. Handing the foreground straight back (below)
    // was only a partial fix: injection can land before the game window exists, in which case the
    // handback silently fails -- the 2026-08-15 run logged exactly that ("game window not found to
    // restore focus"), then bounced focus 19 times and spent 24.7s unfocused MID ROOM LOAD, which
    // alone was ~20% of a 120.5s load.
    //
    // Nothing needs the console: PatchLog (Includes.h) writes every diagnostic to 2025patch.log,
    // flushed per line, and survives a crash. So default to not creating a window at all -- there is
    // no focus to steal if there is no window. The std::cout calls in Patches.h then write to an
    // unopened stdout, which is a silent no-op; each is already mirrored by a PatchLog line.
    if (!RR::Config::EnableConsole) {
        PatchLog("[Console] disabled (EnableConsole=false); diagnostics -> 2025patch.log only");
        return;
    }

    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    HWND con = GetConsoleWindow();
    if (con) {
        ShowWindow(con, SW_SHOWNOACTIVATE);                                    // visible, not activated
        SetWindowPos(con, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    EnumWindows(FindGameWindowProc, 0);
    if (g_gameWindow) {
        ShowWindow(g_gameWindow, SW_SHOW);
        SetForegroundWindow(g_gameWindow);
        SetActiveWindow(g_gameWindow);
        PatchLog("[Console] created without stealing focus; game window=%p restored", g_gameWindow);
    } else {
        PatchLog("[Console] created; game window not found to restore focus");
    }
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        // Config first: it comes from 2025patch.ini next to the game exe and is read before any hook
        // exists, so the first request/DNS lookup already sees the configured backend -- and before
        // CreateConsole, which is now one of the things it decides. PatchLog is file-based, so the
        // config lines are recorded either way.
        RR::Config::Load();
        CreateConsole();
        PatchLog("[DllMain] attached, resolving + patching");
        RR::Patches::Resolve();
        RR::Patches::Patch();
        PatchLog("[DllMain] patch complete");
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

