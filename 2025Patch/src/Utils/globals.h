#pragma once
#include "Inc/Includes.h"

uintptr_t GA = (uintptr_t)GetModuleHandleA("GameAssembly.dll");
const IMAGE_NT_HEADERS* NtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(GA + reinterpret_cast<IMAGE_DOS_HEADER*>(GA)->e_lfanew);

uintptr_t Referee = (uintptr_t)GetModuleHandleA("Referee.dll");

uint64_t* RetAddr = nullptr;

template <typename T>
T read(void* obj, std::ptrdiff_t offset) {
	return *(T*)((std::uint8_t*)obj + offset);
}

template <typename T>
void set(void* obj, std::ptrdiff_t offset, T val) {
	*(T*)((std::uint8_t*)obj + offset) = val;
}

char* ReadIl2CppString(Il2cppString* il2cppString) {

    if (il2cppString == nullptr) {
        return nullptr;
    }

    // NOTE: do NOT trust il2cppString->Length (struct offset 0). On this il2cpp layout offset 0 is
    // the klass pointer, not the length -- it reads as a huge garbage value. The original code got
    // away with it only because wcstombs_s stops at the string's null terminator regardless of the
    // count. So we do the same deliberately: convert the null-terminated wchar_t buffer (at wchar
    // offset 0xA = byte 0x14, the real char data) with a SAFE fixed cap instead of a garbage length.
    // This avoids both the std::bad_alloc abort (huge alloc) AND wrongly rejecting valid strings.
    const size_t maxChars = 8192;
    const size_t bufferSize = (maxChars + 1) * sizeof(wchar_t);
    char* internalString = new (std::nothrow) char[bufferSize];   // nothrow: return null, never throw
    if (!internalString) {
        return nullptr;
    }
    size_t convertedChars = 0;

    errno_t result = wcstombs_s(&convertedChars, internalString, bufferSize, reinterpret_cast<wchar_t*>(il2cppString) + 0xA, maxChars);

    if (result != 0) {
        delete[] internalString;
        return nullptr;
    }

    return internalString;
}