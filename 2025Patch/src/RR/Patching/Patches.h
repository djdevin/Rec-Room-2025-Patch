#pragma once
#include "../Methods.h"
#include "../../Utils/scan.h"
#include "../../Utils/deps/spoofcall/RetSpoof.hpp"

void (*nop_O)();
void nop_H() {
	return;
}

void (*SendRequest_O)(void*);
void SendRequest_H(void* request) {
	void* uri = read<void*>(request, 16);

	if (uri) {

		using ToStringFn = Il2cppString * (*)(void*);
		auto fn = reinterpret_cast<ToStringFn>(GA + RR::Methods::System::Uri::ToString);

		Il2cppString* Uri = spoof_call(RetAddr, fn, uri);

		std::string find = "ns.rec.net";
		std::string replace = "ns.cgb.net"; // you can change this to your nameserver
		std::string url = ReadIl2CppString(Uri);

		if (url.find(find) != std::string::npos) {
			url.replace(url.find(find), find.length(), replace);

			using Il2cppStringNewFn = Il2cppString * (*)(const char*);
			auto fn2 = reinterpret_cast<Il2cppStringNewFn>(GA + RR::Methods::Il2cpp::il2cpp_string_new);

			Il2cppString* NewStr = spoof_call(RetAddr, fn2, url.c_str());

			using ObjectGetClassFn = void * (*)(void*);
			auto fnyeah = reinterpret_cast<ObjectGetClassFn>(GA + RR::Methods::Il2cpp::il2cpp_object_get_class);

			void* UriClass = spoof_call(RetAddr, fnyeah, uri);

			using ObjectNewFn = void* (*)(void*);
			auto fnyeah2 = reinterpret_cast<ObjectNewFn>(GA + RR::Methods::Il2cpp::il2cpp_object_new);

			void* NewUri = spoof_call(RetAddr, fnyeah2, UriClass);

			if (NewUri && NewStr) {
				using UriCtor = void (*)(void*, Il2cppString*);
				auto fn = reinterpret_cast<UriCtor>(GA + RR::Methods::System::Uri::ctor);

				spoof_call(RetAddr, fn, NewUri, NewStr);

				set<void*>(request, 16, NewUri);

				uri = NewUri;
			}
		}

		Il2cppString* FinalStr = spoof_call(RetAddr, fn, uri);

		std::cout << "Uri: " << ReadIl2CppString(FinalStr) << std::endl;
	}

	return SendRequest_O(request);
}

namespace RR::Patches {
	void Resolve() {

		RetAddr = PatternScan<uint64_t*>("FF 23", GA, NtHeaders->OptionalHeader.SizeOfImage);
		auto assm = GetModuleHandle("GameAssembly.dll");

		RR::Methods::Il2cpp::il2cpp_string_new = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_string_new")) - GA;
		RR::Methods::Il2cpp::il2cpp_object_get_class = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_object_get_class")) - GA;
		RR::Methods::Il2cpp::il2cpp_object_new = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_object_new")) - GA;

		std::cout << "il2cpp_string_new -> 0x" << std::hex << RR::Methods::Il2cpp::il2cpp_string_new << std::endl;
		std::cout << "il2cpp_object_get_class -> 0x" << std::hex << RR::Methods::Il2cpp::il2cpp_object_get_class << std::endl;
		std::cout << "il2cpp_object_new -> 0x" << std::hex << RR::Methods::Il2cpp::il2cpp_object_new << std::endl;
	}

	void Patch() {

		MH_Initialize();

		MH_CreateHook((void*)(Referee + RR::Methods::Referee::Check1), &nop_H, (LPVOID*)&nop_O);
		MH_EnableHook((void*)(Referee + RR::Methods::Referee::Check1));

		MH_CreateHook((void*)(Referee + RR::Methods::Referee::Check2), &nop_H, (LPVOID*)&nop_O);
		MH_EnableHook((void*)(Referee + RR::Methods::Referee::Check2));

		MH_CreateHook((void*)(Referee + RR::Methods::Referee::Check3), &nop_H, (LPVOID*)&nop_O);
		MH_EnableHook((void*)(Referee + RR::Methods::Referee::Check3));

		MH_CreateHook((void*)(Referee + RR::Methods::Referee::Check4), &nop_H, (LPVOID*)&nop_O);
		MH_EnableHook((void*)(Referee + RR::Methods::Referee::Check4));

		MH_CreateHook((void*)(GA + RR::Methods::LegacyTlsAuthentication::NotifyServerCertificate), &nop_H, (LPVOID*)&nop_O);
		MH_EnableHook((void*)(GA + RR::Methods::LegacyTlsAuthentication::NotifyServerCertificate));

		MH_CreateHook((void*)(GA + RR::Methods::HTTPRequest::SendRequest), &SendRequest_H, (LPVOID*)&SendRequest_O);
		MH_EnableHook((void*)(GA + RR::Methods::HTTPRequest::SendRequest));
	}
}