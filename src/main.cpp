#include <Arduino.h>
#include <HX711.h>

const uint8_t HX711_DOUT_PIN = D2;
const uint8_t HX711_SCK_PIN = D3;

HX711 scale;

void setup() {
    Serial.begin(115200);
    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
}

void loop() {
    if (scale.is_ready()) {
        long reading = scale.read();
        Serial.println(reading);
    }
    delay(200);
}
