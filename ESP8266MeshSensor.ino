
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>

#include "Mesh.h"
// Update these with values suitable for your network.
#undef BUILTIN_LED
#define BUILTIN_LED 15

Mesh mesh;
void setup() {
    pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
    Serial.begin(115200);
    delay(5000);
    mesh.setup();
}

void loop() {
    mesh.loop();
}
