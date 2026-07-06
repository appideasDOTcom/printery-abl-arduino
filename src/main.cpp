#include <Arduino.h>
#include <HX711.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "wifi_credentials.h"

//------------------------------------------------------
// HX711 Pin Definitions
//------------------------------------------------------

const uint8_t HX711_DOUT = D6;   // GPIO12
const uint8_t HX711_SCK  = D5;   // GPIO14

HX711 scale;

//------------------------------------------------------
// WiFi / OTA configuration
//------------------------------------------------------

constexpr char OTA_HOSTNAME[] = "printery-abl-probe";
constexpr uint32_t WIFI_STATUS_LOG_INTERVAL_MS = 5000;

// Set once ArduinoOTA.begin() has run, so loop() only pays for
// ArduinoOTA.handle() after WiFi actually connects.
bool otaEnabled = false;

// Throttles the pre-connection status log in loop(); unused once connected.
uint32_t lastWifiStatusLogMs = 0;

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

// Kicks off the WiFi connection and returns immediately. Connection happens
// asynchronously in the background (the ESP8266 SDK retries on its own), so
// bad or unreachable credentials never block setup() or loop() — HX711
// init/reads proceed regardless of WiFi outcome. A future retry/backoff
// policy can watch WiFi.status() in loop() without changing this function.
void beginWiFi()
{
    // Modem sleep saves power but delays incoming packets, which would add
    // jitter to loop() timing once WiFi is handled every iteration.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("WiFi connection started in background, SSID: ");
    Serial.println(WIFI_SSID);
}

void setupOTA()
{
    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() { Serial.println("OTA update starting"); });
    ArduinoOTA.onEnd([]() { Serial.println("OTA update complete"); });
    ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error [%u]\n", error); });

    ArduinoOTA.begin();

    Serial.println("OTA ready");
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

    beginWiFi();
}

//------------------------------------------------------

void loop()
{
    // Cheap on every iteration whether or not WiFi ever connects: one
    // status check until connected, then handle() once OTA is live. Never
    // blocks, so probe reads below are unaffected by WiFi/OTA state. The
    // status log stops for good once otaEnabled is set.
    if (!otaEnabled)
    {
        wl_status_t wifiStatus = WiFi.status();

        if (wifiStatus == WL_CONNECTED)
        {
            Serial.print("WiFi connected, IP: ");
            Serial.println(WiFi.localIP());

            setupOTA();
            otaEnabled = true;
        }
        else if (millis() - lastWifiStatusLogMs >= WIFI_STATUS_LOG_INTERVAL_MS)
        {
            lastWifiStatusLogMs = millis();

            Serial.print("WiFi status: ");
            Serial.println(wifiStatusToString(wifiStatus));
        }
    }

    if (otaEnabled)
    {
        ArduinoOTA.handle();
    }

    if (scale.is_ready())
    {
        long raw = scale.read();

        // Serial.print("Raw: ");
        // Serial.println(raw);
		Serial.print("*");
    }
    else
    {
        Serial.println("HX711 NOT READY");
    }

    delay(100);
}