#include <Arduino.h>
#include <HX711.h>

//------------------------------------------------------
// HX711 Pin Definitions
//------------------------------------------------------

const uint8_t HX711_DOUT = D6;   // GPIO12
const uint8_t HX711_SCK  = D5;   // GPIO14

HX711 scale;

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
}

//------------------------------------------------------

void loop()
{
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