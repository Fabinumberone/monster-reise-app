// Online-Updater: neueste Version auf GitHub prüfen, CIA laden und direkt installieren.
// HTTPS über libcurl + mbedTLS (der Systemdienst httpc kann GitHubs TLS nicht zuverlässig),
// Zertifikate aus dem eingebetteten CA-Bündel von curl.se (data/cacert.pem).
#pragma once
#include <functional>
#include <string>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace updater {

// Internet starten (einmalig); false, wenn keine Verbindung möglich (err gesetzt).
bool init(std::string& err);
void exit();

// Neueste Version (Tag ohne „v“, z. B. „0.3.0“); false bei Fehler.
// timeoutSec > 0: Gesamtzeit begrenzen (Prüfung beim Start soll nicht lange aufhalten).
bool latestVersion(std::string& version, std::string& err, long timeoutSec = 0);

// a < b? (Versionsnummern „x.y.z“)
bool isNewer(const std::string& candidate, const std::string& installed);

// Lädt die neueste CIA und installiert sie auf der SD. progress(geladen, gesamt) darf zeichnen.
bool downloadAndInstall(const std::function<void(double, double)>& progress, std::string& err);

} // namespace updater
