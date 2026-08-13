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

std::string ReadIl2CppString(Il2cppString* il2cppString) {
	if (il2cppString == nullptr)
		return {};

	int32_t length = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(il2cppString) + 0x10);

	if (length < 0 || length > 8192) {
		return {};
	}

	std::wstring wide(reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(il2cppString) + 0x14),
		static_cast<size_t>(length));

	size_t bufferSize = (length + 1) * sizeof(wchar_t);
	std::vector<char> buf(bufferSize);
	size_t convertedChars = 0;
	errno_t result = wcstombs_s(&convertedChars, buf.data(), bufferSize, wide.c_str(), length);

	if (result != 0) {
		return {};
	}

	return std::string(buf.data());
}
