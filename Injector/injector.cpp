// Standalone injector for the Rec Room 2025 archival patch.
//
// Waits for Recroom_Release.exe, waits until the game has loaded GameAssembly.dll
// and Referee.dll (the patch resolves both at attach time), then loads the patch
// DLL with CreateRemoteThread + LoadLibraryW so its DllMain runs normally.
//
// No third-party tools required. Ship injector.exe + the patch DLL in the same folder.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>

static const wchar_t* kProcessName = L"Recroom_Release.exe";
static const wchar_t* kDefaultDll  = L"2025Patch.dll";
// How long the window lingers after a successful run, just long enough to read
// the last line. Failures still wait on Enter instead.
static const std::chrono::milliseconds kExitDelay{1500};

// Modules the patch needs present before it can hook anything.
static const wchar_t* kRequiredModules[] = { L"GameAssembly.dll", L"Referee.dll" };

static DWORD FindProcessId(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

// Returns true if the named module is loaded in the target process.
static bool ProcessHasModule(DWORD pid, const wchar_t* moduleName) {
    // The module snapshot briefly fails with ERROR_BAD_LENGTH while the target's
    // module list is changing; retry a few times before giving up.
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_BAD_LENGTH) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            return false;
        }

        MODULEENTRY32W mod{};
        mod.dwSize = sizeof(mod);

        bool found = false;
        if (Module32FirstW(snap, &mod)) {
            do {
                if (_wcsicmp(mod.szModule, moduleName) == 0) {
                    found = true;
                    break;
                }
            } while (Module32NextW(snap, &mod));
        }
        CloseHandle(snap);
        return found;
    }
    return false;
}

static bool AllRequiredModulesLoaded(DWORD pid) {
    for (const wchar_t* m : kRequiredModules) {
        if (!ProcessHasModule(pid, m)) return false;
    }
    return true;
}

static bool Inject(DWORD pid, const std::wstring& dllPath) {
    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!proc) {
        std::wcout << L"[!] OpenProcess failed (" << GetLastError()
                   << L"). Try running the injector as administrator.\n";
        return false;
    }

    bool ok = false;
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);

    LPVOID remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::wcout << L"[!] VirtualAllocEx failed (" << GetLastError() << L").\n";
        CloseHandle(proc);
        return false;
    }

    if (!WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr)) {
        std::wcout << L"[!] WriteProcessMemory failed (" << GetLastError() << L").\n";
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    // kernel32.dll loads at the same address in every process on a given boot,
    // so LoadLibraryW's address here is valid in the target too.
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(k32, "LoadLibraryW"));

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (thread) {
        WaitForSingleObject(thread, INFINITE);

        DWORD exitCode = 0;  // low 32 bits of the returned HMODULE
        GetExitCodeThread(thread, &exitCode);
        CloseHandle(thread);

        // The thread exit code alone is unreliable on x64 (only 32 bits of the
        // HMODULE), so confirm by re-scanning the module list.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::wstring dllName = dllPath.substr(dllPath.find_last_of(L"\\/") + 1);
        ok = ProcessHasModule(pid, dllName.c_str());
        if (!ok && exitCode != 0) ok = true;
    } else {
        std::wcout << L"[!] CreateRemoteThread failed (" << GetLastError() << L").\n";
    }

    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"Rec Room 2025 patch injector\n";
    std::wcout << L"----------------------------\n";

    // Resolve the DLL path: first CLI arg, else <injector folder>\2025Patch.dll.
    std::wstring dllPath;
    if (argc > 1) {
        dllPath = argv[1];
    } else {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring dir(exePath);
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
        dllPath = dir + kDefaultDll;
    }

    // Turn into a full path and confirm it exists before doing anything else.
    wchar_t fullDll[MAX_PATH]{};
    if (!GetFullPathNameW(dllPath.c_str(), MAX_PATH, fullDll, nullptr) ||
        GetFileAttributesW(fullDll) == INVALID_FILE_ATTRIBUTES) {
        std::wcout << L"[!] Patch DLL not found: " << dllPath << L"\n";
        std::wcout << L"    Put the DLL next to this injector, or pass its path as an argument.\n";
        std::wcout << L"\nPress Enter to exit...";
        std::wcin.get();
        return 1;
    }
    dllPath = fullDll;
    std::wcout << L"[*] Patch DLL: " << dllPath << L"\n";

    // Wait for the game process.
    std::wcout << L"[*] Waiting for " << kProcessName << L" (launch Rec Room now)...\n";
    DWORD pid = 0;
    while ((pid = FindProcessId(kProcessName)) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::wcout << L"[+] Found game, PID " << pid << L"\n";

    // Bail early if the patch is already loaded so we never double-inject
    // (that would apply every hook twice and crash the game).
    std::wstring dllName = dllPath.substr(dllPath.find_last_of(L"\\/") + 1);
    if (ProcessHasModule(pid, dllName.c_str())) {
        std::wcout << L"[=] " << dllName << L" is already loaded. Nothing to do.\n";
        std::this_thread::sleep_for(kExitDelay);
        return 0;
    }

    // Wait for the game to finish loading the modules the patch depends on.
    std::wcout << L"[*] Waiting for the game to finish loading...\n";
    while (!AllRequiredModulesLoaded(pid)) {
        // If the game closes while we wait, stop.
        if (FindProcessId(kProcessName) != pid) {
            std::wcout << L"[!] Game process exited before it finished loading.\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // Small settle delay so the modules are fully initialized, not just mapped.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::wcout << L"[*] Injecting...\n";
    if (!Inject(pid, dllPath)) {
        // Only stall on failure, so the reason stays on screen.
        std::wcout << L"[!] Injection failed. See messages above.\n";
        std::wcout << L"\nPress Enter to exit...";
        std::wcin.get();
        return 1;
    }

    std::wcout << L"[+] Done. The patch is loaded.\n";
    std::this_thread::sleep_for(kExitDelay);
    return 0;
}
