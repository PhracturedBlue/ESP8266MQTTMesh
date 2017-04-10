
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "ESP8266MQTTMesh.h"
#include "Utils.h"

#define GREEN_LED   15 //MTDO
#define RELAY       12 //MTDI
#define BUTTON       0 //GPIO0
#define TEMPERATURE  2 //GPIO2

#define HAS_TEMPERATURE 1

OneWire oneWire(TEMPERATURE);
DallasTemperature ds18b20(&oneWire);

ESP8266MQTTMesh mesh;
bool relayState = false;
int  heartbeat  = 300000;
float temperature = 0.0;

void read_config();
void save_config();
void callback(String topic, String msg);
String build_json();

void setup() {
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RELAY,     OUTPUT);
    pinMode(BUTTON,     INPUT);
    Serial.begin(115200);
    delay(5000);
    mesh.setCallback(callback);
    mesh.setup();
    //mesh.setup will initialize the filesystem
    if (SPIFFS.exists("/config")) {
        read_config();
    }
    digitalWrite(RELAY, relayState);
}

void loop() {
    static unsigned long pressed = 0;
    static unsigned long lastSend = 0;
    static bool needToSend = false;
    mesh.loop();
    unsigned long now = millis();
    if (! digitalRead(BUTTON))  {
        if(pressed == 0) {
            relayState = ! relayState;
            digitalWrite(RELAY, relayState);
            save_config();
            needToSend = true;
        }
        pressed = now;
    } else if (pressed && now - pressed > 100) {
        pressed = 0;
    }
    if (needToSend) {
        String data = build_json();
        mesh.publish("status", data);
    }   
}

void callback(String topic, String msg) {
}

String build_json() {
    String msg = "{";
    msg += " \"relay\":\"" + String(relayState ? "ON" : "OFF") + "\"";
    if (HAS_TEMPERATURE) {
        msg += ", \"temp\":" + String(temperature, 2);
    }
    msg += "}";
    return msg;
}

void read_config() {
    File f = SPIFFS.open("/config", "r");
    if (! f) {
        Serial.println("Failed to read config");
        return;
    }
    while(f.available()) {
        String s     = f.readStringUntil('\n');
        String key   = getValue(s, '=', 0);
        String value = getValue(s, '=', 1);
        if (key == "RELAY") {
            relayState = value == "0" ? 0 : 1;
        }
        if (key == "HEARTBEAT") {
            heartbeat = value.toInt();
            if (heartbeat < 1000) {
                heartbeat = 1000;
            } else if (heartbeat > 60 * 60 * 1000) {
                heartbeat = 5 * 60 * 1000;
            }
        }
    }
    f.close();
}

void save_config() {
    File f = SPIFFS.open("/config", "w");
    if (! f) {
        Serial.println("Failed to write config");
        return;
    }
    f.print("RELAY=" + String(relayState ? "1" : "0") + "\n");
    f.print("HEARTBEAT=" + String(heartbeat) + "\n");
    f.close();
}
