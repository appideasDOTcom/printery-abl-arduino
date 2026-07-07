#include <Arduino.h>
#include <HX711.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include "wifi_credentials.h"

//------------------------------------------------------
// HX711 Pin Definitions
//------------------------------------------------------

const uint8_t HX711_DOUT = D6;   // GPIO12
const uint8_t HX711_SCK  = D5;   // GPIO14

HX711 scale;

//------------------------------------------------------
// Live log (mirrored to the web viewer)
//------------------------------------------------------

constexpr size_t LOG_LINE_MAX_LEN = 96;

char latestLogLine[LOG_LINE_MAX_LEN] = "";
uint32_t latestLogSeq = 0;

// Every discrete status/reading line goes through here so it's visible to
// the web log viewer. No Serial output: once WiFi is up this is the only
// place these lines go, keeping Serial off the hot path entirely.
void logLine(const char *line)
{
    // Not strncpy: it zero-pads the rest of the buffer on every call, which
    // is wasted work when this runs every loop iteration.
    size_t len = strlen(line);
    if (len > LOG_LINE_MAX_LEN - 1)
    {
        len = LOG_LINE_MAX_LEN - 1;
    }
    memcpy(latestLogLine, line, len);
    latestLogLine[len] = '\0';

    latestLogSeq++;
}

//------------------------------------------------------
// WiFi / OTA configuration
//------------------------------------------------------

constexpr char OTA_HOSTNAME[] = "printery-abl-probe";
constexpr uint32_t WIFI_STATUS_LOG_INTERVAL_MS = 5000;

// How long to wait for a STA connection (compiled-in or EEPROM-saved
// credentials) before giving up and falling back to WAP captive-portal mode.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

//------------------------------------------------------
// WAP captive portal configuration
//------------------------------------------------------

constexpr uint16_t WIFI_AP_DNS_PORT  = 53;
constexpr uint16_t WIFI_AP_HTTP_PORT = 80;

// AP SSID is this prefix plus the last two bytes of the softAP MAC address,
// e.g. "printery-abl-3F2A", so multiple units don't collide on a shared network.
constexpr char WIFI_AP_SSID_PREFIX[] = "printery-abl-";
constexpr size_t WIFI_AP_SSID_MAX_LEN = sizeof(WIFI_AP_SSID_PREFIX) + 4; // prefix + 4 hex chars + nul

// How long to let the "credentials saved" response actually leave the TCP
// stack before rebooting. Checked via millis() in loop() rather than delay()
// so it never blocks the probe read path.
constexpr uint32_t WIFI_CREDENTIALS_SAVE_RESTART_DELAY_MS = 1500;

char wifiApSsid[WIFI_AP_SSID_MAX_LEN] = "";

DNSServer captiveDnsServer;
ESP8266WebServer captivePortalServer(WIFI_AP_HTTP_PORT);

//------------------------------------------------------
// Persisted WiFi credentials (EEPROM-emulated flash)
//------------------------------------------------------

constexpr uint8_t WIFI_SSID_MAX_LEN = 32;     // 802.11 SSID limit
constexpr uint8_t WIFI_PASSWORD_MAX_LEN = 64; // WPA2-PSK passphrase limit

// Distinguishes "EEPROM holds a previously-saved credential set" from
// "EEPROM is blank/erased flash (0xFF...)" so a fresh board falls back to
// wifi_credentials.h instead of trying to connect with garbage.
constexpr uint16_t WIFI_CREDENTIALS_MAGIC = 0x57C0;

struct StoredWifiCredentials
{
    uint16_t magic;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
};

constexpr size_t EEPROM_SIZE_BYTES = sizeof(StoredWifiCredentials);

//------------------------------------------------------
// WiFi state machine
//------------------------------------------------------

enum class WifiMode
{
    Connecting,
    Connected,
    AccessPoint
};

WifiMode wifiMode = WifiMode::Connecting;

// Set when beginWiFiSTA() kicks off a connection attempt; used to measure
// the WIFI_CONNECT_TIMEOUT_MS fallback window.
uint32_t wifiConnectStartMs = 0;

// Throttles the pre-connection status log in loop(); unused once connected
// or once in AccessPoint mode.
uint32_t lastWifiStatusLogMs = 0;

bool wifiRestartPending = false;
uint32_t wifiRestartAtMs = 0;

//------------------------------------------------------

const char *wifiStatusToString(wl_status_t status)
{
    switch (status)
    {
        case WL_IDLE_STATUS:     return "idle";
        case WL_NO_SSID_AVAIL:   return "SSID not found";
        case WL_SCAN_COMPLETED:  return "scan completed";
        case WL_CONNECTED:       return "connected";
        case WL_CONNECT_FAILED:  return "connect failed (wrong password or auth rejected)";
        case WL_CONNECTION_LOST: return "connection lost";
        case WL_WRONG_PASSWORD:  return "wrong password";
        case WL_DISCONNECTED:    return "disconnected";
        default:                 return "unknown";
    }
}

// Reads any previously-saved credentials out of EEPROM-emulated flash.
// Returns false (leaving the output buffers untouched) if none are stored.
bool loadStoredWifiCredentials(char *ssidOut, size_t ssidOutLen, char *passwordOut, size_t passwordOutLen)
{
    StoredWifiCredentials stored;
    EEPROM.get(0, stored);

    if (stored.magic != WIFI_CREDENTIALS_MAGIC)
    {
        return false;
    }

    // Guard against corrupt flash content even when the magic happens to match.
    stored.ssid[WIFI_SSID_MAX_LEN] = '\0';
    stored.password[WIFI_PASSWORD_MAX_LEN] = '\0';

    strncpy(ssidOut, stored.ssid, ssidOutLen - 1);
    ssidOut[ssidOutLen - 1] = '\0';
    strncpy(passwordOut, stored.password, passwordOutLen - 1);
    passwordOut[passwordOutLen - 1] = '\0';

    return true;
}

void saveWifiCredentials(const char *ssid, const char *password)
{
    StoredWifiCredentials stored;
    stored.magic = WIFI_CREDENTIALS_MAGIC;

    strncpy(stored.ssid, ssid, WIFI_SSID_MAX_LEN);
    stored.ssid[WIFI_SSID_MAX_LEN] = '\0';
    strncpy(stored.password, password, WIFI_PASSWORD_MAX_LEN);
    stored.password[WIFI_PASSWORD_MAX_LEN] = '\0';

    EEPROM.put(0, stored);
    EEPROM.commit();
}

// Prefers credentials saved via the captive portal; falls back to the
// compiled-in wifi_credentials.h defaults when EEPROM has none saved yet.
void resolveWifiCredentials(char *ssidOut, size_t ssidOutLen, char *passwordOut, size_t passwordOutLen)
{
    if (loadStoredWifiCredentials(ssidOut, ssidOutLen, passwordOut, passwordOutLen))
    {
        logLine("Using WiFi credentials saved via captive portal");
        return;
    }

    logLine("No saved WiFi credentials; using compiled-in defaults");
    strncpy(ssidOut, WIFI_SSID, ssidOutLen - 1);
    ssidOut[ssidOutLen - 1] = '\0';
    strncpy(passwordOut, WIFI_PASSWORD, passwordOutLen - 1);
    passwordOut[passwordOutLen - 1] = '\0';
}

// Kicks off the WiFi connection and returns immediately. Connection happens
// asynchronously in the background (the ESP8266 SDK retries on its own), so
// bad or unreachable credentials never block setup() or loop() — HX711
// init/reads proceed regardless of WiFi outcome. serviceWifi() watches
// WiFi.status() in loop() and falls back to WAP mode on timeout.
void beginWiFiSTA(const char *ssid, const char *password)
{
    // Modem sleep saves power but delays incoming packets, which would add
    // jitter to loop() timing once WiFi is handled every iteration.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    char line[LOG_LINE_MAX_LEN];
    snprintf(line, sizeof(line), "WiFi connection started in background, SSID: %s", ssid);
    logLine(line);
}

void setupOTA()
{
    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() { logLine("OTA update starting"); });
    ArduinoOTA.onEnd([]() { logLine("OTA update complete"); });
    ArduinoOTA.onError([](ota_error_t error) {
        char line[LOG_LINE_MAX_LEN];
        snprintf(line, sizeof(line), "OTA error [%u]", error);
        logLine(line);
    });

    ArduinoOTA.begin();

    logLine("OTA ready");
}

//------------------------------------------------------
// Log web server (served once connected to a real WiFi network)
//------------------------------------------------------

constexpr uint16_t LOG_SERVER_HTTP_PORT = 80;

// How often the browser polls for a new log line, and how long it waits
// before retrying after a failed request (e.g. mid-OTA-update).
constexpr uint32_t LOG_POLL_INTERVAL_MS = 300;
constexpr uint32_t LOG_POLL_RETRY_MS = 1000;

ESP8266WebServer logServer(LOG_SERVER_HTTP_PORT);

// Static page: an auto-scrolling frame that polls /log and appends any new
// line. No history is kept server-side, so this only ever shows current data,
// not a full transcript.
void handleLogPageRoot()
{
    char html[900];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>printery-abl-probe log</title>"
        "<style>"
        "body{background:#111;color:#0f0;font-family:monospace;margin:0;}"
        "#log{height:100vh;overflow-y:auto;padding:8px;box-sizing:border-box;white-space:pre-wrap;}"
        "</style></head><body>"
        "<div id='log'></div>"
        "<script>"
        "var seen=-1;var el=document.getElementById('log');"
        "function poll(){fetch('/log').then(function(r){return r.json();}).then(function(d){"
        "if(d.seq!==seen){seen=d.seq;el.textContent+=d.line+String.fromCharCode(10);el.scrollTop=el.scrollHeight;}"
        "setTimeout(poll,%u);"
        "}).catch(function(){setTimeout(poll,%u);});}"
        "poll();"
        "</script></body></html>",
        LOG_POLL_INTERVAL_MS, LOG_POLL_RETRY_MS);

    logServer.send(200, "text/html", html);
}

void handleLogData()
{
    char json[LOG_LINE_MAX_LEN + 32];
    snprintf(json, sizeof(json), "{\"seq\":%lu,\"line\":\"%s\"}",
             static_cast<unsigned long>(latestLogSeq), latestLogLine);

    logServer.send(200, "application/json", json);
}

void setupLogServer()
{
    logServer.on("/", HTTP_GET, handleLogPageRoot);
    logServer.on("/log", HTTP_GET, handleLogData);
    logServer.begin();

    logLine("Log web server ready");
}

//------------------------------------------------------
// WAP captive portal
//------------------------------------------------------

// Builds the HTML page each time it's served rather than in the probe hot
// path: String use here only runs while the rare WAP setup page is active.
String captivePortalPageHtml(const char *errorMessage)
{
    String html;
    html.reserve(1024);

    html += "<!DOCTYPE html><html><head>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>printery-abl-probe WiFi setup</title>"
            "<style>"
            "body{font-family:sans-serif;background:#f2f2f2;margin:0;padding:24px;}"
            "#card{max-width:400px;margin:0 auto;background:#fff;border:1px solid #ccc;"
            "border-radius:6px;padding:16px 20px;}"
            "input{width:100%;box-sizing:border-box;padding:8px;margin:8px 0;}"
            ".error{color:#b00020;font-weight:bold;}"
            "</style></head><body><div id='card'>"
            "<h3>printery-abl-probe WiFi setup</h3>";

    if (errorMessage != nullptr)
    {
        html += "<p class='error'>";
        html += errorMessage;
        html += "</p>";
    }

    html += "<form action='/save' method='post'>"
            "<input type='text' name='ssid' placeholder='WiFi SSID' autocapitalize='off' autocorrect='off' autocomplete='off'>"
            "<input type='password' name='password' placeholder='WiFi password' autocapitalize='off' autocorrect='off' autocomplete='off'>"
            "<input type='submit' value='Connect'>"
            "</form>"
            "</div></body></html>";

    return html;
}

void handleCaptivePortalRoot()
{
    captivePortalServer.send(200, "text/html", captivePortalPageHtml(nullptr));
}

void handleCaptivePortalConnect()
{
    String submittedSsid = captivePortalServer.arg("ssid");
    String submittedPassword = captivePortalServer.arg("password");

    if (submittedSsid.length() == 0)
    {
        captivePortalServer.send(200, "text/html", captivePortalPageHtml("SSID is required."));
        return;
    }

    saveWifiCredentials(submittedSsid.c_str(), submittedPassword.c_str());

    String html =
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body><p>Saved. Rebooting to connect to \"" + submittedSsid + "\"...</p>"
        "<p>If it fails to connect, this access point will reappear.</p>"
        "</body></html>";
    captivePortalServer.send(200, "text/html", html);

    wifiRestartPending = true;
    wifiRestartAtMs = millis();
}

void buildApSsid()
{
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    snprintf(wifiApSsid, sizeof(wifiApSsid), "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
}

void startCaptivePortal()
{
    WiFi.mode(WIFI_AP);
    buildApSsid();
    WiFi.softAP(wifiApSsid);

    captiveDnsServer.start(WIFI_AP_DNS_PORT, "*", WiFi.softAPIP());

    captivePortalServer.on("/", HTTP_GET, handleCaptivePortalRoot);
    captivePortalServer.on("/save", HTTP_POST, handleCaptivePortalConnect);
    captivePortalServer.onNotFound(handleCaptivePortalRoot);
    captivePortalServer.begin();

    char line[LOG_LINE_MAX_LEN];
    snprintf(line, sizeof(line), "WiFi connect failed; started WAP captive portal, SSID: %s", wifiApSsid);
    logLine(line);
    snprintf(line, sizeof(line), "Browse to http://%s", WiFi.softAPIP().toString().c_str());
    logLine(line);
}

//------------------------------------------------------

// Cheap on every iteration regardless of WiFi outcome: one status/timeout
// check while connecting, then either OTA handling or captive-portal
// servicing, never blocking — so probe reads in loop() are unaffected.
void serviceWifi()
{
    switch (wifiMode)
    {
        case WifiMode::Connecting:
        {
            wl_status_t status = WiFi.status();

            if (status == WL_CONNECTED)
            {
                char line[LOG_LINE_MAX_LEN];
                snprintf(line, sizeof(line), "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
                logLine(line);

                setupOTA();
                setupLogServer();
                wifiMode = WifiMode::Connected;
            }
            else if (millis() - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS)
            {
                startCaptivePortal();
                wifiMode = WifiMode::AccessPoint;
            }
            else if (millis() - lastWifiStatusLogMs >= WIFI_STATUS_LOG_INTERVAL_MS)
            {
                lastWifiStatusLogMs = millis();

                char line[LOG_LINE_MAX_LEN];
                snprintf(line, sizeof(line), "WiFi status: %s", wifiStatusToString(status));
                logLine(line);
            }
            break;
        }

        case WifiMode::Connected:
            ArduinoOTA.handle();
            logServer.handleClient();
            break;

        case WifiMode::AccessPoint:
            captiveDnsServer.processNextRequest();
            captivePortalServer.handleClient();

            if (wifiRestartPending && millis() - wifiRestartAtMs >= WIFI_CREDENTIALS_SAVE_RESTART_DELAY_MS)
            {
                ESP.restart();
            }
            break;
    }
}

//------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" HX711 / K2 Strain Gauge Test");
    Serial.println("========================================");

    scale.begin(HX711_DOUT, HX711_SCK);

    Serial.print("Waiting for HX711");

    while (!scale.is_ready())
    {
        Serial.print(".");
        delay(250);
    }

    Serial.println();
    Serial.println("HX711 Ready");
    Serial.println();

    EEPROM.begin(EEPROM_SIZE_BYTES);

    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    resolveWifiCredentials(ssid, sizeof(ssid), password, sizeof(password));

    beginWiFiSTA(ssid, password);
    wifiConnectStartMs = millis();
}

//------------------------------------------------------

void loop()
{
    serviceWifi();

    if (scale.is_ready())
    {
        long raw = scale.read();

        char line[LOG_LINE_MAX_LEN];
        snprintf(line, sizeof(line), "Raw: %ld", raw);
        logLine(line);
    }
    else
    {
        logLine("HX711 NOT READY");
    }

    delay(100);
}
