#pragma once
#include "../Utils/globals.h"

// =================================================================================================
// Runtime configuration -- 2025patch.ini, read from the game exe's folder (same place PatchLog
// writes 2025patch.log).
//
// The mirror hosts and the Photon backend selection used to be literals/constexpr in Patches.h, so
// pointing the patch at a different backend -- or A/B-ing against Photon Cloud -- meant a rebuild.
// They are read here instead, once, at DLL attach.
//
// INI + GetPrivateProfileString is deliberate: it is a Win32 API, so there is no parser to write and
// no dependency to add, and the file stays hand-editable next to the exe. If the file is missing it
// is created with the defaults below, so the knobs are discoverable without reading the source.
//
// Everything is best-effort -- a missing file, an unreadable one, or a malformed value all fall back
// to the compiled-in default rather than leaving an empty host or a nonsense port. Whatever is
// finally in effect is logged, so 2025patch.log always says which backend the run actually used.
// =================================================================================================
namespace RR::Config {

	constexpr const char* kFileName = "2025patch.ini";
	constexpr const char* kSection  = "config";

	// --- Defaults: the recflare mirror, self-hosted Photon. In effect when there is no ini/value. ---

	char ApiHost[128]    = "ns.recflare.net";      // replaces ns.rec.net in BestHTTP request URIs
	char PhotonHost[128] = "photon.recflare.net";  // getaddrinfo target for *.photonengine / exitgames

	// false -> self-hosted Photon: DNS for the Photon name server is redirected to PhotonHost.
	// true  -> real Photon Cloud: the DNS redirect is skipped so ns.exitgames.com resolves normally,
	//          and the CloudAppId* values below are injected into AppSettings just before connect
	//          (the server-supplied ids belong to the self-hosted deployment and are invalid on
	//          Cloud). See the PHOTON BACKEND SELECTION block in Patches.h for what this test found.
	bool UsePhotonCloud = false;

	// Self-hosted only. 0 = leave whatever the server/client supplied. This is the port of the
	// INITIAL connect (name server / master); the master still hands out its own game-server ports.
	int PhotonPort = 0;

	// Diagnostic console (AllocConsole). Off by default and worth leaving off: the console window
	// steals foreground focus, Unity throttles hard while unfocused, and that measurably wrecked
	// room-load times -- see CreateConsole in main.cpp for the numbers. Everything it would print is
	// already in 2025patch.log, so this is only for live poking.
	bool EnableConsole = false;

	// THIRD-PARTY telemetry / analytics / crash-reporting hosts (RudderStack, Statsig, Backtrace,
	// Unity cloud). Most of these are still LIVE (verified 2026-08-17: statsigapi.net, submit.backtrace.io
	// and both cloud.unity3d.com hosts all resolve), so leaving this on is as much about not shipping
	// an archival session's telemetry and crash dumps to unrelated companies as it is about latency.
	// true = fail those lookups instantly at the getaddrinfo hook. Substrings are IsDeadHost in Patches.h.
	//
	// ⚠️ Only ever list hosts that are genuinely external and unwanted. Blocking a *.recflare.net host
	// does NOT help: the client retries transport failures with backoff on the single serial
	// System.Net.Http queue, so a blocked host costs the same as an unreachable one. Only a real,
	// fast response stops the retries -- stub it server-side and drop it from the list instead.
	bool BlockDeadHosts = true;

	// Diagnostic tracing: the request-pump probes ([Pump]/[Send]), Photon operation/status/event
	// tracers, HttpClient path logger and BestHTTP response logger. Off by default -- they are chatty
	// and every question they were built to answer is now recorded in CLAUDE.md and in the comment
	// blocks beside each hook. Kept rather than deleted because they are the only way this project has
	// ever managed to see inside the serial request queue or catch a server-pushed Photon event.
	bool EnableTracing = false;

	// Cloud only (UsePhotonCloud=true). Empty = keep the server-supplied value for that one.
	char CloudAppIdRealtime[64] = "8f322bdb-2b1f-4c27-a232-01436f43d14e";  // Photon Realtime / PUN
	char CloudAppIdVoice[64]    = "6b4682e1-a1a9-4e04-b44a-6db0049a4df3";  // Photon Voice
	char CloudAppIdChat[64]     = "55fae86e-0459-4f97-bf6c-39c9341da6ef";  // Photon Chat
	char CloudFixedRegion[32]   = "";  // e.g. "us"; empty = let the client pick via name server

	// Path to the ini, next to the game exe (Recroom_Release.exe) -- NOT the DLL and not the CWD.
	// GetPrivateProfileString requires a full path; given a bare name it searches the Windows
	// directory instead, which would silently ignore the user's file.
	static void ConfigPath(char* out, size_t cch) {
		GetModuleFileNameA(nullptr, out, (DWORD)cch);
		char* slash = strrchr(out, '\\');
		if (slash) strcpy_s(slash + 1, cch - (slash + 1 - out), kFileName);
	}

	static void Trim(char* s) {
		char* start = s;
		while (*start == ' ' || *start == '\t' || *start == '"') ++start;
		char* end = start + strlen(start);
		while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '"')) --end;
		*end = '\0';
		if (start != s) memmove(s, start, strlen(start) + 1);
	}

	// Host values additionally lose a scheme and any path: they are used as bare host names
	// (getaddrinfo takes a node name, not a URL), so "https://host/" has to become "host" rather than
	// fail at connect time. Returns false if nothing usable is left.
	static bool SanitizeHost(char* s) {
		Trim(s);
		char* start = s;
		if (_strnicmp(start, "https://", 8) == 0) start += 8;
		else if (_strnicmp(start, "http://", 7) == 0) start += 7;
		if (start != s) memmove(s, start, strlen(start) + 1);
		if (char* slash = strchr(s, '/')) *slash = '\0';
		Trim(s);
		return *s != '\0';
	}

	// Current value doubles as the default, so an absent key always keeps it.
	static void ReadHost(const char* path, const char* key, char* value, size_t cch) {
		char buf[256] = {};
		GetPrivateProfileStringA(kSection, key, value, buf, (DWORD)sizeof(buf), path);
		if (!SanitizeHost(buf)) {
			PatchLog("[Config] %s is blank/invalid, keeping default %s", key, value);
			return;
		}
		strcpy_s(value, cch, buf);
	}

	// For the app ids and the region, where EMPTY IS MEANINGFUL ("keep whatever the server sent") --
	// so unlike a host, a blank value is honoured rather than replaced by the default.
	static void ReadString(const char* path, const char* key, char* value, size_t cch) {
		char buf[256] = {};
		GetPrivateProfileStringA(kSection, key, value, buf, (DWORD)sizeof(buf), path);
		Trim(buf);
		if (strlen(buf) >= cch) {
			PatchLog("[Config] %s is too long (%zu chars), keeping default", key, strlen(buf));
			return;
		}
		strcpy_s(value, cch, buf);
	}

	static void ReadBool(const char* path, const char* key, bool& value) {
		char buf[64] = {};
		GetPrivateProfileStringA(kSection, key, value ? "true" : "false", buf, (DWORD)sizeof(buf), path);
		Trim(buf);
		if (!_stricmp(buf, "true") || !_stricmp(buf, "1") || !_stricmp(buf, "yes") || !_stricmp(buf, "on")) {
			value = true;
		} else if (!_stricmp(buf, "false") || !_stricmp(buf, "0") || !_stricmp(buf, "no") || !_stricmp(buf, "off")) {
			value = false;
		} else {
			PatchLog("[Config] %s='%s' is not a boolean, keeping default %s", key, buf, value ? "true" : "false");
		}
	}

	static void ReadPort(const char* path, const char* key, int& value) {
		// GetPrivateProfileInt returns the default for a missing key and 0 for a non-numeric one; 0 is
		// also the legitimate "leave it alone" value here, so a typo'd port lands on the safe option.
		int v = (int)GetPrivateProfileIntA(kSection, key, value, path);
		if (v < 0 || v > 65535) {
			PatchLog("[Config] %s=%d out of range (0-65535), keeping default %d", key, v, value);
			return;
		}
		value = v;
	}

	// Written only when the file does not exist -- never overwrites a user's edits.
	static void WriteDefaultFile(const char* path) {
		FILE* f = nullptr;
		fopen_s(&f, path, "w");
		if (!f) return;
		fprintf(f,
			"; Rec Room 2025 patch configuration. Applied at injection.\n"
			"; Delete a line (or the whole file) to fall back to the built-in defaults.\n"
			"\n"
			"[%s]\n"
			"\n"
			"; Backend hosts. Bare host names -- no scheme, no path.\n"
			"; ApiHost rewrites ns.rec.net in the game's API requests.\n"
			"ApiHost=%s\n"
			"; PhotonHost is the DNS target for Photon (*.photonengine / exitgames / photonindustries).\n"
			"PhotonHost=%s\n"
			"\n"
			"; false = self-hosted Photon (PhotonHost above). true = real Photon Cloud: the DNS\n"
			"; redirect is skipped and the CloudAppId* values below are used instead.\n"
			"UsePhotonCloud=%s\n"
			"\n"
			"; Self-hosted only. Port of the initial Photon connect; 0 = leave the supplied port.\n"
			"PhotonPort=%d\n"
			"\n"
			"; Debug console window. Costs load time -- it steals focus and Unity throttles while\n"
			"; unfocused. Everything it prints is already in 2025patch.log.\n"
			"EnableConsole=%s\n"
			"\n"
			"; Fail third-party telemetry/analytics host lookups (RudderStack, Statsig, Backtrace,\n"
			"; Unity cloud) instantly. Most are still live, so this also keeps an archival session's\n"
			"; telemetry and crash dumps from reaching unrelated companies. Do not add recflare hosts.\n"
			"BlockDeadHosts=%s\n"
			"\n"
			"; Verbose diagnostic tracing: request-pump probes, Photon operation/event tracers,\n"
			"; HttpClient paths, BestHTTP responses. Chatty; only needed when investigating.\n"
			"EnableTracing=%s\n"
			"\n"
			"; Photon Cloud only (UsePhotonCloud=true). Empty = keep the server-supplied value.\n"
			"CloudAppIdRealtime=%s\n"
			"CloudAppIdVoice=%s\n"
			"CloudAppIdChat=%s\n"
			"; e.g. us -- empty lets the client pick a region via the name server.\n"
			"CloudFixedRegion=%s\n",
			kSection, ApiHost, PhotonHost, UsePhotonCloud ? "true" : "false", PhotonPort,
			EnableConsole ? "true" : "false", BlockDeadHosts ? "true" : "false",
			EnableTracing ? "true" : "false",
			CloudAppIdRealtime, CloudAppIdVoice, CloudAppIdChat, CloudFixedRegion);
		fclose(f);
	}

	void Load() {
		char path[MAX_PATH] = {};
		ConfigPath(path, MAX_PATH);

		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
			WriteDefaultFile(path);
			PatchLog("[Config] %s not found; wrote defaults to %s", kFileName, path);
		}

		// The section was [hosts] before the Photon knobs were added. Such a file parses fine and
		// applies NOTHING, which looks identical to "config ignored" in the log -- so call it out.
		char legacy[256] = {};
		GetPrivateProfileStringA("hosts", "ApiHost", "", legacy, (DWORD)sizeof(legacy), path);
		if (*legacy) PatchLog("[Config] ignoring legacy [hosts] section -- rename it to [%s]", kSection);

		ReadHost(path, "ApiHost",    ApiHost,    sizeof(ApiHost));
		ReadHost(path, "PhotonHost", PhotonHost, sizeof(PhotonHost));
		ReadBool(path, "UsePhotonCloud", UsePhotonCloud);
		ReadBool(path, "EnableConsole",  EnableConsole);
		ReadBool(path, "BlockDeadHosts", BlockDeadHosts);
		ReadBool(path, "EnableTracing",  EnableTracing);
		ReadPort(path, "PhotonPort",     PhotonPort);
		ReadString(path, "CloudAppIdRealtime", CloudAppIdRealtime, sizeof(CloudAppIdRealtime));
		ReadString(path, "CloudAppIdVoice",    CloudAppIdVoice,    sizeof(CloudAppIdVoice));
		ReadString(path, "CloudAppIdChat",     CloudAppIdChat,     sizeof(CloudAppIdChat));
		ReadString(path, "CloudFixedRegion",   CloudFixedRegion,   sizeof(CloudFixedRegion));

		PatchLog("[Config] %s", path);
		PatchLog("[Config] ApiHost=%s PhotonHost=%s UsePhotonCloud=%s PhotonPort=%d EnableConsole=%s BlockDeadHosts=%s EnableTracing=%s",
			ApiHost, PhotonHost, UsePhotonCloud ? "true" : "false", PhotonPort,
			EnableConsole ? "true" : "false", BlockDeadHosts ? "true" : "false",
			EnableTracing ? "true" : "false");
		if (UsePhotonCloud) {
			PatchLog("[Config] CloudAppIdRealtime=%s Voice=%s Chat=%s FixedRegion=%s",
				CloudAppIdRealtime, CloudAppIdVoice, CloudAppIdChat,
				*CloudFixedRegion ? CloudFixedRegion : "(server-supplied)");
		}
	}
}
