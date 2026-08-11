#include "RR/Patching/Patches.h"

void CreateConsole() {
    AllocConsole();
	FILE* f;
	freopen_s(&f, "CONOUT$", "w", stdout);
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateConsole();
        RR::Patches::Resolve();
        RR::Patches::Patch();
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

