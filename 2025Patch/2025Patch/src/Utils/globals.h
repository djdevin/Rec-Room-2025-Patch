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

    size_t length = static_cast<size_t>(il2cppString->Length);
    const size_t bufferSize = (length + 1) * sizeof(wchar_t);
    char* internalString = new char[bufferSize];
    size_t convertedChars = 0;

    errno_t result = wcstombs_s(&convertedChars, internalString, bufferSize, reinterpret_cast<wchar_t*>(il2cppString) + 0xA, length);

    if (result != 0) {
        delete[] internalString;
        return nullptr;
    }

    return internalString;
}