#pragma once
#include <Windows.h>
#include <iostream>
#include <cstdint>
#include <tlhelp32.h>
#include <vector>


#include "../deps/minhook/include/MinHook.h"

struct Il2cppString {
	int Length;
	wchar_t s[1024];
};
