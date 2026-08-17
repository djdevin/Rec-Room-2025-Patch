#pragma once
#include "../Methods.h"
#include "../../Utils/scan.h"
#include "../../Utils/deps/spoofcall/RetSpoof.hpp"
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

void (*nop_O)();
void nop_H() {
	return;
}

void (*SendRequest_O)(void*);
void SendRequest_H(void* request) {
	static long s_n = 0;
	long n = ++s_n;
	PatchLog("[SendRequest] #%ld enter request=%p", n, request);

	// Everything the hook does before forwarding is best-effort: it must NEVER take down the game.
	// An uncaught C++ exception here (bad_alloc, or std::string from a null pointer during the
	// injection/boot race) hits std::terminate -> the intermittent "Runtime Error! ...unusual way"
	// abort. So wrap the rewrite in try/catch and always fall through to the original request.
	try {
	void* uri = read<void*>(request, 16);

	if (uri) {

		using ToStringFn = Il2cppString * (*)(void*);
		auto fn = reinterpret_cast<ToStringFn>(GA + RR::Methods::System::Uri::ToString);

		Il2cppString* Uri = spoof_call(RetAddr, fn, uri);

		std::string find = "ns.rec.net";
		std::string replace = "ns.recflare.net"; // recflare nameserver (serves the *.recflare.net service map)

		// ReadIl2CppString returns a new[]-allocated buffer or nullptr. `std::string s = nullptr` is
		// undefined behavior (this was the intermittent crash) -- guard it, and free the buffer (the
		// original code leaked it every request).
		char* raw = ReadIl2CppString(Uri);
		if (!raw) {
			PatchLog("[SendRequest] #%ld uri unreadable, passing through", n);
			SendRequest_O(request);
			PatchLog("[SendRequest] #%ld returned", n);
			return;
		}
		std::string url(raw);
		delete[] raw;

		PatchLog("[SendRequest] #%ld url=%s", n, url.c_str());
		if (url.find(find) != std::string::npos) {
			url.replace(url.find(find), find.length(), replace);
			PatchLog("[SendRequest] #%ld REWRITE -> %s", n, url.c_str());

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

		// Log the (already-computed) url. The previous code did a SECOND spoof_call ToString here
		// purely to print the final URI -- that redundant spoofed il2cpp call was the crash site: a
		// managed exception unwinding through the spoofed return address yields STATUS_INVALID_
		// DISPOSITION (0xC0000026) and kills the process deep in home-screen loading. We already have
		// `url` (post-rewrite), so just print it -- no extra il2cpp call, no extra crash surface.
		std::cout << "Uri: " << url << std::endl;
	}
	}
	catch (...) {
		// Rewrite failed somehow -- never abort the game; just forward the request unchanged.
		PatchLog("[SendRequest] #%ld exception in hook, passing through unchanged", n);
	}

	PatchLog("[SendRequest] #%ld -> original", n);
	SendRequest_O(request);
	PatchLog("[SendRequest] #%ld returned", n);
	return;
}

// ---------------------------------------------------------------------------------------------
// Response logger: correlate URL -> HTTP status -> actual body.
//
// A Utf8Json failure ("expected:'Number Token', actual:'{'") names the expected TYPE but never the
// endpoint, so attributing a payload to a URL by eyeballing the body is guesswork. This hooks
// HTTPRequest.CallCallback (fires when the response is in hand) and logs the request pointer -- the
// same pointer SendRequest_H already logged next to the URL -- plus status and a body preview. That
// makes the mapping exact instead of inferred.
//
// Deliberately does NOT call Uri.ToString here: a second spoofed il2cpp call on a path that may be
// unwinding is what previously corrupted the unwind and killed the process (0xC0000026). The request
// pointer is enough to join against the SendRequest log line.
// ---------------------------------------------------------------------------------------------
void (*CallCallback_O)(void*);
void CallCallback_H(void* request) {
	// Run the real callback FIRST. HTTPResponse.Data is a lazily-populated backing field: reading it
	// at hook entry (before the client's own callback touches Data/DataAsText) returned null for
	// 142 of 143 responses. After the callback has run it is materialised.
	CallCallback_O(request);

	__try {
		if (request) {
			void* resp = read<void*>(request, RR::Offsets::Request_Response);
			int status = resp ? read<int32_t>(resp, RR::Offsets::Response_StatusCode) : -1;
			int len = 0;
			char preview[513] = {};
			if (resp) {
				void* data = read<void*>(resp, RR::Offsets::Response_Data);
				len = read<int32_t>(resp, RR::Offsets::Response_DataLength);
				if (data && len > 0) {
					const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data) + RR::Offsets::Array_Data;
					int n = len < 512 ? len : 512;
					for (int i = 0; i < n; i++) {
						char c = static_cast<char>(bytes[i]);
						preview[i] = (c >= 32 && c < 127) ? c : '.';
					}
					preview[n] = 0;
				}
				else {
					// Fallback: the string form, populated when the client reads DataAsText.
					Il2cppString* txt = read<Il2cppString*>(resp, RR::Offsets::Response_DataAsText);
					if (txt) {
						char* s = ReadIl2CppString(txt);
						if (s) {
							int i = 0; for (; s[i] && i < 512; i++) preview[i] = s[i];
							preview[i] = 0; len = i;
							delete[] s;
						}
					}
				}
			}
			PatchLog("[HTTP<-] request=%p status=%d len=%d body=%s", request, status, len, preview);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------------------------
// HttpClient request logger (RecNet's System.Net.Http path, CBACIMLIBPF.IBJKIAMJCDN).
//
// Both overloads are STATIC, so there is no `this`: arg1=HttpMethod*, arg2=service enum,
// arg3=path string (r8), rest follow. Every parameter is <=8 bytes (Nullable<int32> and
// CancellationToken are both 8), so declaring them as pointer-sized slots forwards correctly under
// the Win64 ABI. We only read the path and pass everything straight through.
// ---------------------------------------------------------------------------------------------
typedef void* (*Req9_t)(void*, uint64_t, Il2cppString*, void*, uint64_t, uint64_t, void*, void*, uint64_t, void*);
typedef void* (*Req10_t)(void*, uint64_t, Il2cppString*, void*, void*, uint64_t, uint64_t, void*, void*, uint64_t, void*);
Req9_t  Req9_O = nullptr;
Req10_t Req10_O = nullptr;

static void LogHttpClientPath(const char* which, uint64_t svc, Il2cppString* path) {
	__try {
		if (path) {
			char* p = ReadIl2CppString(path);
			if (p) { PatchLog("[HttpClient/%s] svc=%llu path=%s", which, svc, p); delete[] p; }
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

void* Req9_H(void* m, uint64_t svc, Il2cppString* path, void* a4, uint64_t a5, uint64_t a6, void* a7, void* a8, uint64_t a9, void* mi) {
	LogHttpClientPath("9", svc, path);
	return Req9_O(m, svc, path, a4, a5, a6, a7, a8, a9, mi);
}
void* Req10_H(void* m, uint64_t svc, Il2cppString* path, void* a4, void* a5, uint64_t a6, uint64_t a7, void* a8, void* a9, uint64_t a10, void* mi) {
	LogHttpClientPath("10", svc, path);
	return Req10_O(m, svc, path, a4, a5, a6, a7, a8, a9, a10, mi);
}

// ---------------------------------------------------------------------------------------------
// Force Photon PAYLOAD encryption (disable datagram encryption).
//
// See RR::Methods::Photon in Methods.h for the reasoning. Short version: Luxon only implements
// payload encryption; with datagram encryption every post-handshake packet fails its ENet CRC /
// protocol validation and is dropped WITHOUT a log, which is why the server showed "Established
// encryption" then 43s of silence while the client retried every 5.03s and finally threw
// "Unable to send message!". Returning false keeps the client on the payload path Luxon speaks.
//
// Logs the ORIGINAL value once, so we confirm the client really was asking for datagram encryption
// (orig=1) instead of assuming it.
// ---------------------------------------------------------------------------------------------
bool (*IsTransportEncrypted_O)(void*, void*);
bool IsTransportEncrypted_H(void* self, void* mi) {
	static bool logged = false;
	if (!logged) {
		logged = true;
		bool orig = false;
		__try { orig = IsTransportEncrypted_O(self, mi); } __except (EXCEPTION_EXECUTE_HANDLER) {}
		PatchLog("[Crypto] EnetPeer.IsTransportEncrypted original=%d -> forcing FALSE (payload encryption)", orig ? 1 : 0);
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Image content-signature bypass.  See RR::Methods::ImageSignature in Methods.h for the full
// reasoning. Short version: mirrored images still carry rec.net's original `content-signature`
// RSA header, the client verifies it against a baked-in public key, and no mirror can ever satisfy
// it -- so the check is unsatisfiable rather than merely failing, and is dead weight for archival.
//
// Why this blocks ROOM LOAD and not just artwork: image fetches and the room save-data blob share
// one request pump (CBACIMLIBPF.KOMEOKGFOBP, which drains a Queue). In the failing run every svc=7
// request issued BEFORE the first signature failure (17:54:51.2) reached BestHTTP, and the first
// one issued after it -- the room blob, 17:54:52.4 -- never did; it is absent from both our
// SendRequest log and mitmproxy. That is a stalled pump, which is why the loading bar sits at
// DOWNLOADING_DETAILS until the 30s timeout cancels FetchRoomLoadDetails > getRoomSaveData.
//
// Logs the ORIGINAL verdict once (same discipline as IsTransportEncrypted above): if that line ever
// reads original=1 then signatures were verifying fine and this hook is masking a different fault.
// ---------------------------------------------------------------------------------------------
bool (*VerifyImageSig_O)(void*, void*, void*);
bool VerifyImageSig_H(void* data, void* sig, void* mi) {
	static bool logged = false;
	if (!logged) {
		logged = true;
		bool orig = false;
		__try { orig = VerifyImageSig_O(data, sig, mi); } __except (EXCEPTION_EXECUTE_HANDLER) {}
		PatchLog("[ImgSig] HPJKKCCECLH.BLDFMLMAFLH original=%d -> forcing TRUE", orig ? 1 : 0);
	}
	return true;
}

// =============================================================================================
// PHOTON BACKEND SELECTION
//
// UsePhotonCloud = false -> self-hosted Photon server (Luxon). DNS for the Photon name server is
//                           redirected to PhotonHost below.
// UsePhotonCloud = true  -> real Photon Cloud. The DNS redirect is skipped so ns.exitgames.com
//                           resolves normally, and the app ids below are injected into AppSettings
//                           just before connect (the server-supplied ids belong to the self-hosted
//                           deployment and are not valid on Cloud).
//
// This exists to isolate a failure: against Luxon the client joins, gets ~4 SetProperties answered,
// then receives NO responses for ~50s and dies with "Unable to send message!". Running the same
// client against Photon Cloud says whether that is a Luxon bug or a client/patch bug -- worth
// knowing before changing the server.
//
// Paste the three GUIDs from the Photon dashboard. Leave a value empty to keep whatever the server
// supplied for that one.
// =============================================================================================
// Set true ONLY to A/B against real Photon Cloud. That test has been run: Cloud reproduced the
// failure identically, proving Photon/Luxon are NOT the cause -- the blocker is the room's missing
// save data. Left false so the client targets the self-hosted server.
constexpr bool UsePhotonCloud = false;

const char* CloudAppIdRealtime = "8f322bdb-2b1f-4c27-a232-01436f43d14e";  // Photon Realtime / PUN app id
const char* CloudAppIdVoice    = "6b4682e1-a1a9-4e04-b44a-6db0049a4df3";  // Photon Voice app id
const char* CloudAppIdChat     = "55fae86e-0459-4f97-bf6c-39c9341da6ef";  // Photon Chat app id
const char* CloudFixedRegion   = "";  // e.g. "us"; empty = let the client pick via name server

const char* PhotonHost = "photon.recflare.net"; // recflare self-hosted Photon server

// ---------------------------------------------------------------------------------------------
// Inject Photon Cloud app ids into AppSettings just before the client connects.
//
// The app ids normally come from the server's connection-info response and are only meaningful to
// that deployment (Luxon logs them as "rf-..."), so they must be replaced wholesale to reach Cloud.
// Writing the fields here -- rather than rewriting the HTTP response -- keeps it to one seam and
// avoids touching JSON. Setting a field is a plain pointer store of a fresh il2cpp string.
// ---------------------------------------------------------------------------------------------
bool (*ConnectUsingSettings_O)(void*, void*, void*);

static void SetAppSettingsString(void* settings, int offset, const char* value, const char* label) {
	if (!value || !*value) return;   // empty = leave the server-supplied value alone
	using Il2cppStringNewFn = Il2cppString * (*)(const char*);
	auto fn = reinterpret_cast<Il2cppStringNewFn>(GA + RR::Methods::Il2cpp::il2cpp_string_new);
	Il2cppString* s = spoof_call(RetAddr, fn, value);
	if (!s) { PatchLog("[PhotonCloud] failed to allocate string for %s", label); return; }
	set<void*>(settings, offset, s);
	PatchLog("[PhotonCloud] %s = %s", label, value);
}

bool ConnectUsingSettings_H(void* self, void* settings, void* mi) {
	__try {
		if (settings && UsePhotonCloud) {
			SetAppSettingsString(settings, RR::Offsets::AppSettings::AppIdRealtime, CloudAppIdRealtime, "AppIdRealtime");
			SetAppSettingsString(settings, RR::Offsets::AppSettings::AppIdVoice,    CloudAppIdVoice,    "AppIdVoice");
			SetAppSettingsString(settings, RR::Offsets::AppSettings::AppIdChat,     CloudAppIdChat,     "AppIdChat");
			SetAppSettingsString(settings, RR::Offsets::AppSettings::FixedRegion,   CloudFixedRegion,   "FixedRegion");

			// Cloud requires the name server (it is how a region is resolved), and any Server/Port
			// override left over from the self-hosted path would send us straight back to Luxon.
			set<bool>(settings, RR::Offsets::AppSettings::UseNameServer, true);
			set<void*>(settings, RR::Offsets::AppSettings::Server, nullptr);
			set<int32_t>(settings, RR::Offsets::AppSettings::Port, 0);
			PatchLog("[PhotonCloud] UseNameServer=true, Server cleared -> connecting to Photon Cloud");
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
	return ConnectUsingSettings_O(self, settings, mi);
}

bool IsPhotonHost(const std::string& node) {
	return node.find("photonengine") != std::string::npos
		|| node.find("exitgames") != std::string::npos
		|| node.find("photonindustries") != std::string::npos;
}

int (WSAAPI* getaddrinfo_O)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
int WSAAPI getaddrinfo_H(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
	if (!UsePhotonCloud && pNodeName && IsPhotonHost(pNodeName)) {
		std::cout << "Photon: " << pNodeName << " -> " << PhotonHost << std::endl;
		PatchLog("[Photon] getaddrinfo %s -> %s", pNodeName, PhotonHost);
		return getaddrinfo_O(PhotonHost, pServiceName, pHints, ppResult);
	}
	// Log every other lookup too. The room-save download does NOT go through BestHTTP (it uses the
	// System.Net.Http client, CBACIMLIBPF), so our SendRequest hook is blind to it -- but its DNS
	// still comes through here, which is how we find out what host it is actually contacting.
	if (pNodeName) PatchLog("[DNS] %s", pNodeName);
	return getaddrinfo_O(pNodeName, pServiceName, pHints, ppResult);
}

int (WSAAPI* GetAddrInfoW_O)(PCWSTR, PCWSTR, const ADDRINFOW*, PADDRINFOW*);
int WSAAPI GetAddrInfoW_H(PCWSTR pNodeName, PCWSTR pServiceName, const ADDRINFOW* pHints, PADDRINFOW* ppResult) {
	if (pNodeName) {
		char node[NI_MAXHOST] = {};
		size_t converted = 0;
		wcstombs_s(&converted, node, pNodeName, _TRUNCATE);

		if (!UsePhotonCloud && IsPhotonHost(node)) {
			std::cout << "Photon: " << node << " -> " << PhotonHost << std::endl;

			PatchLog("[Photon] GetAddrInfoW %s -> %s", node, PhotonHost);
			wchar_t wideHost[NI_MAXHOST] = {};
			mbstowcs_s(&converted, wideHost, PhotonHost, _TRUNCATE);
			return GetAddrInfoW_O(wideHost, pServiceName, pHints, ppResult);
		}
		PatchLog("[DNS] %s (W)", node);
	}
	return GetAddrInfoW_O(pNodeName, pServiceName, pHints, ppResult);
}

// ---------------------------------------------------------------------------------------------
// Photon protocol tracer (diagnostic). Luxon works with the 2023 client but the 2025 client reaches
// the game server (5056) and then times out with "Unable to connect to game session" -- and Luxon
// logs no error, meaning the 2025 client is waiting on an operation/response Luxon doesn't provide.
// These two call-through hooks (RVAs from the RuntimeDump, build verified via SendRequest=0x77E0950)
// print every operation the client SENDS and every connection STATUS it gets, so we can see exactly
// what the 2025 client does that the 2023 client didn't:
//   PhotonPeer.SendOperation(byte opCode, Dictionary, SendOptions)  GA+0x7534610
//   PeerBase.EnqueueStatusCallback(StatusCode)                      GA+0x752C540
// ---------------------------------------------------------------------------------------------

static const char* PhotonOpName(unsigned op) {
	switch (op) {
	case 219: return "ExchangeKeysForEncryption";
	case 220: return "GetRegions";
	case 221: return "WebRpc";
	case 222: return "GetGameList";
	case 224: return "FindFriends";
	case 225: return "JoinRandomGame";
	case 226: return "JoinGame(JoinRoom)";
	case 227: return "CreateGame";
	case 228: return "LeaveLobby";
	case 229: return "JoinLobby";
	case 230: return "Authenticate";
	case 231: return "AuthenticateOnce";
	case 248: return "ChangeGroups";
	case 250: return "Rpc";
	case 251: return "GetProperties";
	case 252: return "SetProperties";
	case 253: return "RaiseEvent";
	case 254: return "Leave";
	case 255: return "Join(legacy)";
	default:  return "?";
	}
}

static const char* PhotonStatusName(unsigned s) {
	switch (s) {
	case 1022: return "SecurityExceptionOnConnect";
	case 1023: return "ExceptionOnConnect";
	case 1024: return "Connect";
	case 1025: return "Disconnect";
	case 1026: return "Exception";
	case 1030: return "SendError";
	case 1039: return "ExceptionOnReceive";
	case 1040: return "TimeoutDisconnect";
	case 1041: return "DisconnectByServerTimeout";
	case 1042: return "DisconnectByServerUserLimit";
	case 1043: return "DisconnectByServerLogic";
	case 1044: return "DisconnectByServerReasonUnknown";
	case 1048: return "EncryptionEstablished";
	case 1049: return "EncryptionFailedToEstablish";
	case 1050: return "ServerAddressInvalid";
	case 1051: return "DnsExceptionOnConnect";
	default:   return "?";
	}
}

// PhotonPeer.SendOperation(this, byte opCode, Dictionary* params, SendOptions, MethodInfo*) -> bool.
// SendOptions is <=8 bytes (fits R9); forward all args verbatim.
typedef bool (*SendOp_t)(void* self, uint64_t opCode, void* params, uint64_t sendOptions, void* mi);
SendOp_t SendOp_O = nullptr;
bool SendOp_H(void* self, uint64_t opCode, void* params, uint64_t sendOptions, void* mi) {
	unsigned op = (unsigned)(opCode & 0xFF);
	// Log the RESULT, not just the attempt. SendOperation returns false when the peer refuses the
	// send (not connected, outgoing queue full, or payload over the max size) -- nothing reaches the
	// wire and no response can ever come back. Photon Cloud and the self-hosted server BOTH stop
	// answering these identically, which only makes sense if the ops are never actually sent; then
	// "Unable to send message!" means exactly what it says.
	bool ok = SendOp_O(self, opCode, params, sendOptions, mi);
	PatchLog("[Photon] --> SendOperation op=%u (%s) peer=%p sent=%d", op, PhotonOpName(op), self, ok ? 1 : 0);
	return ok;
}

// PeerBase.EnqueueStatusCallback(this, StatusCode statusValue, MethodInfo*) -> void.
typedef void (*EnqStatus_t)(void* self, uint32_t status, void* mi);
EnqStatus_t EnqStatus_O = nullptr;
void EnqStatus_H(void* self, uint32_t status, void* mi) {
	PatchLog("[Photon] <-- Status %u (%s) peer=%p", status, PhotonStatusName(status), self);
	EnqStatus_O(self, status, mi);
}

// OperationResponse layout (ExitGames.Client.Photon.OperationResponse):
//   OperationCode uint8_t @16, ReturnCode int16_t @18, DebugMessage Il2CppString* @24
// Log every response the SERVER sends, decoded, so we can see if Luxon returns an error code the
// client treats as fatal -- and WHICH wire protocol is active (Protocol16 vs Protocol18; the 2023
// vs 2025 client may negotiate different serialization, a prime suspect for "2023 works, 2025 not").
static void LogOpResponse(const char* proto, void* resp) {
	if (!resp) return;
	unsigned opc = *(uint8_t*)((uint8_t*)resp + 16);
	short  rc   = *(int16_t*)((uint8_t*)resp + 18);
	Il2cppString* dbg = *(Il2cppString**)((uint8_t*)resp + 24);
	char* msg = dbg ? ReadIl2CppString(dbg) : nullptr;
	PatchLog("[Photon] <== Resp[%s] op=%u (%s) returnCode=%d%s%s", proto, opc, PhotonOpName(opc), (int)rc,
		msg ? " msg=" : "", msg ? msg : "");
	if (msg) delete[] msg;
}

typedef void* (*DeserResp_t)(void* self, void* stream, int flags, void* mi);
DeserResp_t Deser16_O = nullptr, Deser18_O = nullptr;
void* Deser16_H(void* self, void* stream, int flags, void* mi) {
	void* r = Deser16_O(self, stream, flags, mi);
	__try { LogOpResponse("P16", r); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	return r;
}
void* Deser18_H(void* self, void* stream, int flags, void* mi) {
	void* r = Deser18_O(self, stream, flags, mi);
	__try { LogOpResponse("P18", r); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	return r;
}

namespace RR::Methods::Photon {
	uintptr_t SendOperation = 0x7534610;
	uintptr_t EnqueueStatusCallback = 0x752C540;
	uintptr_t Protocol16_DeserializeOperationResponse = 0x75388D0;
	uintptr_t Protocol18_DeserializeOperationResponse = 0x753E610;
}

namespace RR::Patches {
	void Resolve() {

		RetAddr = PatternScan<uint64_t*>("FF 23", GA, NtHeaders->OptionalHeader.SizeOfImage);
		auto assm = GetModuleHandle("GameAssembly.dll");

		RR::Methods::Il2cpp::il2cpp_string_new = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_string_new")) - GA;
		RR::Methods::Il2cpp::il2cpp_object_get_class = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_object_get_class")) - GA;
		RR::Methods::Il2cpp::il2cpp_object_new = reinterpret_cast<uintptr_t>(spoof_call(RetAddr, GetProcAddress, assm, "il2cpp_object_new")) - GA;

		// To the log, not just the console -- the console is off by default now (see CreateConsole in
		// main.cpp), and a zero here means an export failed to resolve, which is worth keeping.
		PatchLog("[Resolve] il2cpp_string_new=0x%llX il2cpp_object_get_class=0x%llX il2cpp_object_new=0x%llX",
			(unsigned long long)RR::Methods::Il2cpp::il2cpp_string_new,
			(unsigned long long)RR::Methods::Il2cpp::il2cpp_object_get_class,
			(unsigned long long)RR::Methods::Il2cpp::il2cpp_object_new);
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

		// Response logger -- joins to the SendRequest line by request pointer.
		MH_CreateHook((void*)(GA + RR::Methods::HTTPRequest::CallCallback), &CallCallback_H, (LPVOID*)&CallCallback_O);
		MH_EnableHook((void*)(GA + RR::Methods::HTTPRequest::CallCallback));

		// HttpClient path logger -- this is where the room save/asset downloads actually go.
		MH_CreateHook((void*)(GA + RR::Methods::HttpClient::Request9), &Req9_H, (LPVOID*)&Req9_O);
		MH_EnableHook((void*)(GA + RR::Methods::HttpClient::Request9));

		MH_CreateHook((void*)(GA + RR::Methods::HttpClient::Request10), &Req10_H, (LPVOID*)&Req10_O);
		MH_EnableHook((void*)(GA + RR::Methods::HttpClient::Request10));

		// Image content-signature bypass -- mirrored assets can never satisfy rec.net's RSA signature,
		// and the resulting throw stalls the shared request pump that the room save-data blob is
		// queued behind. Install BEFORE anything fetches an image.
		MH_CreateHook((void*)(GA + RR::Methods::ImageSignature::Verify), &VerifyImageSig_H, (LPVOID*)&VerifyImageSig_O);
		MH_EnableHook((void*)(GA + RR::Methods::ImageSignature::Verify));

		// photon doesn't go through HTTPRequest, it resolves ns.photonengine.io etc. and connects over raw sockets,
		// so redirect it at the winsock dns level instead
		LoadLibraryA("ws2_32.dll");

		void* getaddrinfoTarget = nullptr;
		MH_CreateHookApiEx(L"ws2_32", "getaddrinfo", &getaddrinfo_H, (LPVOID*)&getaddrinfo_O, &getaddrinfoTarget);
		MH_EnableHook(getaddrinfoTarget);

		void* GetAddrInfoWTarget = nullptr;
		MH_CreateHookApiEx(L"ws2_32", "GetAddrInfoW", &GetAddrInfoW_H, (LPVOID*)&GetAddrInfoW_O, &GetAddrInfoWTarget);
		MH_EnableHook(GetAddrInfoWTarget);

		// Photon protocol tracer -- logs every operation sent + every connection status, to diagnose
		// the game-session join hang against Luxon.
		MH_CreateHook((void*)(GA + RR::Methods::Photon::SendOperation), &SendOp_H, (LPVOID*)&SendOp_O);
		MH_EnableHook((void*)(GA + RR::Methods::Photon::SendOperation));

		MH_CreateHook((void*)(GA + RR::Methods::Photon::EnqueueStatusCallback), &EnqStatus_H, (LPVOID*)&EnqStatus_O);
		MH_EnableHook((void*)(GA + RR::Methods::Photon::EnqueueStatusCallback));

		// Incoming operation-response tracer (both protocol impls; whichever fires reveals the active
		// serialization AND the server's return codes).
		MH_CreateHook((void*)(GA + RR::Methods::Photon::Protocol16_DeserializeOperationResponse), &Deser16_H, (LPVOID*)&Deser16_O);
		MH_EnableHook((void*)(GA + RR::Methods::Photon::Protocol16_DeserializeOperationResponse));

		// Photon backend selection: inject Cloud app ids just before connect (no-op when
		// UsePhotonCloud is false, but the hook is cheap and keeps one code path).
		MH_CreateHook((void*)(GA + RR::Methods::Photon::ConnectUsingSettings), &ConnectUsingSettings_H, (LPVOID*)&ConnectUsingSettings_O);
		MH_EnableHook((void*)(GA + RR::Methods::Photon::ConnectUsingSettings));

		// NOT hooked: EnetPeer.IsTransportEncrypted. Measured original=0, i.e. the client was ALREADY
		// using payload encryption, so datagram encryption was never the cause of Luxon's silence.
		// Kept only as a recorded dead end so it is not re-tried.

		MH_CreateHook((void*)(GA + RR::Methods::Photon::Protocol18_DeserializeOperationResponse), &Deser18_H, (LPVOID*)&Deser18_O);
		MH_EnableHook((void*)(GA + RR::Methods::Photon::Protocol18_DeserializeOperationResponse));

		PatchLog("[Patch] all hooks installed (Referee x4, TLS, SendRequest, ImgSig, getaddrinfo, GetAddrInfoW, Photon-trace)");
		PatchLog("[Patch] API host=ns.recflare.net  Photon host=%s", PhotonHost);
	}
}