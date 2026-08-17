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
	// BestHTTP.HTTPRequest.CallCallback() -- runs once a response has arrived, so it is the one place
	// where the request AND its response are both in hand. Needed to correlate URL -> status -> body:
	// a deserialize failure only tells you the expected TYPE, never which endpoint produced the bytes.
	uintptr_t CallCallback = 0x77EBAE0;
}

// Photon.Realtime.AppSettings -- the settings object the client hands to its connect call. Field
// offsets from the runtime dump (build 19/07/25); these names are NOT obfuscated.
namespace RR::Offsets::AppSettings {
	constexpr int AppIdRealtime  = 16;   // Il2CppString*
	constexpr int AppIdChat      = 24;   // Il2CppString*
	constexpr int AppIdVoice     = 32;   // Il2CppString*
	constexpr int AppVersion     = 40;   // Il2CppString*
	constexpr int UseNameServer  = 48;   // bool
	constexpr int FixedRegion    = 56;   // Il2CppString*
	constexpr int Server         = 72;   // Il2CppString*
	constexpr int Port           = 80;   // int32
}

// OLPEILEPEAD.JBNCMFDFDLM(AppSettings) -- the "ConnectUsingSettings" seam. Hooking it lets us
// rewrite the app ids (and region/nameserver) immediately before the client connects, which is how
// we point the client at Photon Cloud instead of the self-hosted server.
namespace RR::Methods::Photon {
	uintptr_t ConnectUsingSettings = 0x757E8C0;
}

// Photon transport encryption.
//
// Luxon implements ONLY Photon's PayloadEncryption (it has Encrypt/DecryptPayload, and never reads
// the EncryptionMode=193 parameter the client sends). The 2025 client negotiates DATAGRAM
// encryption, so after the handshake it encrypts the whole ENet datagram -- Luxon then fails CRC /
// protocol validation and drops every packet SILENTLY (those catch blocks only bump a metric).
// Symptom: "Established encryption" followed by ~43s of nothing, client retrying every 5.03s until
// it throws "Unable to send message!". The 2023 client uses payload encryption and works fine.
//
// EnetPeer.IsTransportEncrypted() is the gate the send path consults; forcing it false keeps the
// client on payload encryption, which is exactly what Luxon speaks.
namespace RR::Methods::Photon {
	uintptr_t EnetPeer_IsTransportEncrypted = 0x7521380;
}

// RecNet's System.Net.Http-based client (CBACIMLIBPF). The room-save/asset downloads go through
// THIS, not BestHTTP, so HTTPManager.SendRequest never sees them -- which is why a room join logs
// ~97 requests and not one blob fetch. Both overloads are static; the request path is the 3rd
// argument (r8). Hooking them makes the save-data download visible.
namespace RR::Methods::HttpClient {
	uintptr_t Request9  = 0x7CC50D0;  // IBJKIAMJCDN(HttpMethod, service, path, ...) 9 params
	uintptr_t Request10 = 0x7CC4F20;  // IBJKIAMJCDN(HttpMethod, service, path, ..., Queue) 10 params
}

// Field offsets from the runtime dump (build 19/07/25), for the response logger.
namespace RR::Offsets {
	// BestHTTP.HTTPRequest
	constexpr int Request_Uri        = 16;   // System.Uri*
	constexpr int Request_Response   = 112;  // BestHTTP.HTTPResponse*
	// BestHTTP.HTTPResponse
	constexpr int Response_StatusCode = 24;  // int32
	constexpr int Response_Data       = 72;  // byte[]*
	constexpr int Response_DataLength = 80;  // int32
	constexpr int Response_DataAsText = 96;  // Il2CppString* (populated once the client reads it)
	// il2cpp array: klass(8) + monitor(8) + bounds(8) + max_length(8), payload follows
	constexpr int Array_Data = 32;
}

// Image content-signature verification (HPJKKCCECLH, RecNet.Runtime).
//
// img.recflare.net serves ARCHIVED rec.net assets verbatim, so every image still carries the
// original response header
//     content-signature: key-id=KEY:RSA:p1.rec.net; data=<base64 RSA signature>
// which is what the `?sig=p1` query string selects. The client verifies that signature against an
// RSA public key baked into OGONMIPNEKK() and, on mismatch, throws MELLIJAOIBB "Image signature
// verification failed" out of the async OIKINEOLHBL(url, bool, ct). The bytes served are a valid
// JPEG (FF D8 FF DB) -- it is the signature that cannot be satisfied, because reproducing it would
// need Rec Room's private key. For an archival client the check is unsatisfiable by construction.
//
// Verify is `private static bool BLDFMLMAFLH(byte[] data, byte[] signature)`: a static il2cpp
// method, so the two arrays arrive in RCX/RDX with MethodInfo* in R8. Forcing it true is the fix.
namespace RR::Methods::ImageSignature {
	uintptr_t Verify    = 0x7CF32F0;  // BLDFMLMAFLH(byte[] data, byte[] sig) -> bool
	uintptr_t PublicKey = 0x7CF84E0;  // OGONMIPNEKK() -> RSAParameters   (not hooked; for reference)
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