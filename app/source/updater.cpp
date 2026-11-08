#include "updater.hpp"

#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "cacert_pem.h"

namespace updater {

static const char* API_LATEST = "https://api.github.com/repos/Fabinumberone/monster-reise-app/releases/latest";
static const char* CIA_LATEST = "https://github.com/Fabinumberone/monster-reise-app/releases/latest/download/monster-reise-app.cia";
static const char* USER_AGENT = "MonsterReiseApp/" APP_VERSION;
static constexpr u32 SOC_SIZE = 0x100000;

static std::string g_assetUrl; // API-Adresse der CIA im neuesten Release (aus latestVersion)
static u32* g_soc = nullptr;
static bool g_curl = false;

bool init(std::string& err) {
    if (g_soc) return true;
    u32 wifi = 0;
    if (R_SUCCEEDED(acInit())) {
        ACU_GetWifiStatus(&wifi);
        acExit();
    }
    if (!wifi) {
        err = "Keine WLAN-Verbindung.";
        return false;
    }
    g_soc = static_cast<u32*>(memalign(0x1000, SOC_SIZE));
    if (!g_soc) { err = "Kein Speicher für das Netzwerk."; return false; }
    Result r = socInit(g_soc, SOC_SIZE);
    if (R_FAILED(r)) {
        free(g_soc);
        g_soc = nullptr;
        char b[64];
        std::snprintf(b, sizeof b, "Netzwerk nicht verfügbar (0x%08lX).", static_cast<unsigned long>(r));
        err = b;
        return false;
    }
    if (!g_curl) g_curl = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return true;
}

void exit() {
    if (g_curl) { curl_global_cleanup(); g_curl = false; }
    if (g_soc) { socExit(); free(g_soc); g_soc = nullptr; }
}

static CURL* makeHandle(const char* url) {
    CURL* c = curl_easy_init();
    if (!c) return nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L); // Abbruch, wenn 60 s fast nichts kommt
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_blob ca{const_cast<u8*>(cacert_pem), cacert_pem_size, CURL_BLOB_NOCOPY};
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    return c;
}

static std::string curlError(CURLcode code, const char* buf) {
    std::string e = buf && *buf ? buf : curl_easy_strerror(code);
    return "Verbindung fehlgeschlagen: " + e;
}

static size_t toString(char* p, size_t size, size_t n, void* user) {
    auto* s = static_cast<std::string*>(user);
    if (s->size() > 256 * 1024) return 0; // Antwort der API ist klein
    s->append(p, size * n);
    return size * n;
}

bool latestVersion(std::string& version, std::string& err, long timeoutSec) {
    CURL* c = makeHandle(API_LATEST);
    if (!c) { err = "curl nicht verfügbar."; return false; }
    if (timeoutSec > 0) {
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, timeoutSec);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
    }
    std::string body;
    char eb[CURL_ERROR_SIZE] = {};
    curl_slist* h = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, eb);
    CURLcode r = curl_easy_perform(c);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) { err = curlError(r, eb); return false; }
    // "tag_name": "v0.3.0"
    size_t k = body.find("\"tag_name\"");
    if (k == std::string::npos) { err = "Antwort von GitHub unverständlich."; return false; }
    size_t a = body.find('"', body.find(':', k) + 1);
    size_t b = a == std::string::npos ? a : body.find('"', a + 1);
    if (b == std::string::npos) { err = "Antwort von GitHub unverständlich."; return false; }
    version = body.substr(a + 1, b - a - 1);
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    // API-Adresse der CIA: im Asset-Objekt steht "url" vor "name"
    g_assetUrl.clear();
    size_t n = body.find("\"name\":\"monster-reise-app.cia\"");
    if (n == std::string::npos) n = body.find("\"name\": \"monster-reise-app.cia\"");
    if (n != std::string::npos) {
        size_t u = body.rfind("\"url\":", n);
        size_t q1 = u == std::string::npos ? u : body.find('"', u + 6);
        size_t q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 != std::string::npos) {
            std::string url = body.substr(q1 + 1, q2 - q1 - 1);
            if (url.rfind("https://api.github.com/repos/Fabinumberone/monster-reise-app/releases/assets/", 0) == 0) g_assetUrl = url;
        }
    }
    return true;
}

static std::vector<int> parts(const std::string& v) {
    std::vector<int> out;
    int cur = 0;
    bool any = false;
    for (char ch : v) {
        if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); any = true; }
        else if (ch == '.') { out.push_back(cur); cur = 0; any = false; }
        else break;
    }
    if (any || !out.empty()) out.push_back(cur);
    while (out.size() < 3) out.push_back(0);
    return out;
}

bool isNewer(const std::string& candidate, const std::string& installed) {
    return parts(candidate) > parts(installed);
}

struct Download {
    std::vector<u8> data;
    bool tooBig = false;
    const std::function<void(double, double)>* progress;
    u64 lastDraw = 0;
};

static size_t toMemory(char* p, size_t size, size_t n, void* user) {
    auto* d = static_cast<Download*>(user);
    if (d->data.size() + size * n > 64u * 1024 * 1024) { d->tooBig = true; return 0; } // eine CIA dieser App ist ~1–2 MB
    d->data.insert(d->data.end(), p, p + size * n);
    return size * n;
}

static int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* d = static_cast<Download*>(user);
    u64 t = osGetTime();
    if (t - d->lastDraw >= 100 || (total && now == total)) { // höchstens 10 Bilder je Sekunde
        d->lastDraw = t;
        (*d->progress)(double(now), double(total));
    }
    return aptMainLoop() ? 0 : 1; // beim Schließen über HOME abbrechen
}

static std::string hexErr(const char* what, Result r) {
    char b[96];
    std::snprintf(b, sizeof b, "%s (0x%08lX).", what, static_cast<unsigned long>(r));
    return b;
}

// Erst vollständig in den Speicher laden, dann wie FBI in festen Blöcken installieren: Der
// Installationsdienst lehnt Schreibgrößen ab, wie sie gerade aus dem Netz kommen (0xE0E083F2,
// „misaligned size“, 2026-11-08). So beginnt die Installation auch erst nach vollständigem Download.
bool downloadAndInstall(const std::function<void(double, double)>& progress, std::string& err) {
    // Zwei Wege: erst über die API (Datei-Adresse aus latestVersion), dann über den direkten Link.
    // (2026-11-08: github.com/…/releases/download lieferte stundenlang 504, die API funktionierte.)
    std::vector<std::pair<std::string, bool>> sources;
    if (!g_assetUrl.empty()) sources.push_back({g_assetUrl, true});
    sources.push_back({CIA_LATEST, false});
    Download dl;
    dl.progress = &progress;
    bool ok = false;
    for (auto& [url, api] : sources) {
        dl.data.clear();
        dl.tooBig = false;
        CURL* c = makeHandle(url.c_str());
        char eb[CURL_ERROR_SIZE] = {};
        CURLcode cr = CURLE_FAILED_INIT;
        curl_slist* h = api ? curl_slist_append(nullptr, "Accept: application/octet-stream") : nullptr;
        if (c) {
            if (h) curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toMemory);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &dl);
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
            curl_easy_setopt(c, CURLOPT_XFERINFODATA, &dl);
            curl_easy_setopt(c, CURLOPT_ERRORBUFFER, eb);
            curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 128L * 1024L);
            cr = curl_easy_perform(c);
            curl_easy_cleanup(c);
        }
        if (h) curl_slist_free_all(h);
        if (cr == CURLE_OK && dl.data.size() >= 0x2020 && dl.data[0] == 0x20 && dl.data[1] == 0x20) { ok = true; break; }
        if (cr == CURLE_OK) err = "Download ist keine CIA.";
        else if (dl.tooBig) err = "Download zu groß.";
        else err = cr == CURLE_ABORTED_BY_CALLBACK ? "Abgebrochen." : curlError(cr, eb);
        if (cr == CURLE_ABORTED_BY_CALLBACK) return false;
    }
    if (!ok) return false;

    if (R_FAILED(amInit())) { err = "Installationsdienst (AM) nicht verfügbar."; return false; }
    Handle hnd = 0;
    Result r = AM_StartCiaInstall(MEDIATYPE_SD, &hnd);
    if (R_FAILED(r)) { amExit(); err = hexErr("Installation konnte nicht starten", r); return false; }
    constexpr u32 CHUNK = 0x20000;
    for (u64 off = 0; off < dl.data.size(); off += CHUNK) {
        u32 n = u32(std::min<u64>(CHUNK, dl.data.size() - off)), written = 0;
        r = FSFILE_Write(hnd, &written, off, dl.data.data() + off, n, 0);
        if (R_FAILED(r) || written != n) {
            AM_CancelCIAInstall(hnd);
            amExit();
            err = hexErr("Schreiben fehlgeschlagen", R_FAILED(r) ? r : -1);
            return false;
        }
        progress(double(off + n), double(dl.data.size()));
    }
    r = AM_FinishCiaInstall(hnd); // prüft die CIA; bei Fehler bleibt die alte Version
    amExit();
    if (R_FAILED(r)) { err = hexErr("Installation fehlgeschlagen", r); return false; }
    return true;
}

} // namespace updater
