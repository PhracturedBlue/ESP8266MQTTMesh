
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266MQTTMesh.h>

/* See credentials.h.examle for contents of credentials.h */
#include "credentials.h"


const String networks[]       = NETWORK_LIST;
const char*  network_password = NETWORK_PASSWORD;
const char*  mesh_password    = MESH_PASSWORD;
const String base_ssid        = BASE_SSID;
const char*  mqtt_server      = MQTT_SERVER;
const int    mqtt_port        = MQTT_PORT;
const int    mesh_port        = MESH_PORT;

/* Sonoff POW w/ DS18B20 attached to GPIO2(SDA) */
#define GREEN_LED   15 //MTDO
#define RELAY       12 //MTDI
#define BUTTON       0 //GPIO0
#define DS18B20      2 //GPIO2

#ifdef DS18B20
OneWire oneWire(DS18B20);
DallasTemperature ds18b20(&oneWire);
DeviceAddress ds18b20Address;
#endif

ESP8266MQTTMesh mesh(networks, network_password, mesh_password,
                     &base_ssid, mqtt_server, mqtt_port, mesh_port);

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
#ifdef DS18B20
    ds18b20.begin();
    ds18b20.getAddress(ds18b20Address, 0);
    ds18b20.setWaitForConversion(false);
    ds18b20.requestTemperatures();
#endif
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

#ifdef DS18B20
    if (ds18b20.isConversionAvailable(ds18b20Address)) {
        temperature = ds18b20.getTempF(ds18b20Address);
        ds18b20.requestTemperatures();
    }    
#endif

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
#ifdef DS18B20RE
        msg += ", \"temp\":" + String(temperature, 2);
#endif
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
        String key   = ESP8266MQTTMesh::getValue(s, '=', 0);
        String value = ESP8266MQTTMesh::getValue(s, '=', 1);
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
