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

// Set once ArduinoOTA.begin() has run, so loop() only pays for
// ArduinoOTA.handle() after WiFi actually connects.
bool otaEnabled = false;

//------------------------------------------------------

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

    Serial.println("WiFi connection started in background");
}

void setupOTA()
{
    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() { Serial.println("OTA update starting"); });
    ArduinoOTA.onEnd([]() { Serial.println("OTA update complete"); });
    ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error [%u]\n", error); });

    ArduinoOTA.begin();

    Serial.print("OTA ready, IP: ");
    Serial.println(WiFi.localIP());
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
    // blocks, so probe reads below are unaffected by WiFi/OTA state.
    if (!otaEnabled && WiFi.status() == WL_CONNECTED)
    {
        setupOTA();
        otaEnabled = true;
    }

    if (otaEnabled)
    {
        ArduinoOTA.handle();
    }

    if (scale.is_ready())
    {
        long raw = scale.read();

        Serial.print("Raw: ");
        Serial.println(raw);
    }
    else
    {
        Serial.println("HX711 NOT READY");
    }

    delay(100);
}