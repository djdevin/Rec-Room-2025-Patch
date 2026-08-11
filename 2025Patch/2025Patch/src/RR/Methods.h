#pragma once
#include "../Utils/globals.h"

namespace RR::Methods::Referee {
	uintptr_t Check1 = 0x76E850;
	uintptr_t Check2 = 0x2E6F680;
	uintptr_t Check3 = 0x2E6F670;
	uintptr_t Check4 = 0x2E6F6E0;
}

namespace RR::Methods::LegacyTlsAuthentication {
	uintptr_t NotifyServerCertificate = 0x77E5E30;
}

namespace RR::Methods::HTTPRequest {
	uintptr_t SendRequest = 0x77E0950;
}

namespace RR::Methods::System::Uri {
	uintptr_t ToString = 0x935AF50;
	uintptr_t ctor = 0x935C2A0;
}

namespace RR::Methods::Il2cpp {
	uintptr_t il2cpp_string_new;
	uintptr_t il2cpp_object_get_class;
	uintptr_t il2cpp_object_new;
}