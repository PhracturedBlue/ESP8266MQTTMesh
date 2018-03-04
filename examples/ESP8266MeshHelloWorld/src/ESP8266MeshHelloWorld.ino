#include "credentials.h"
#include <ESP8266MQTTMesh.h>
#include <FS.h>


#ifndef LED_PIN
  #define LED_PIN LED_BUILTIN
#endif


#define      FIRMWARE_ID        0x1337
#define      FIRMWARE_VER       "0.1"
wifi_conn    networks[]       = NETWORK_LIST;
const char*  mesh_password    = MESH_PASSWORD;
const char*  mqtt_server      = MQTT_SERVER;
const int    mqtt_port        = MQTT_PORT;
#if ASYNC_TCP_SSL_ENABLED
const uint8_t *mqtt_fingerprint = MQTT_FINGERPRINT;
bool mqtt_secure = MQTT_SECURE;
  #if MESH_SECURE
  #include "ssl_cert.h"
  #endif
#endif

#ifdef ESP32
String ID  = String((unsigned long)ESP.getEfuseMac());
#else
String ID  = String(ESP.getChipId());
#endif




unsigned long previousMillis = 0;
const long interval = 5000;
int cnt = 0;

// Note: All of the '.set' options below are optional.  The default values can be
// found in ESP8266MQTTMeshBuilder.h
ESP8266MQTTMesh mesh = ESP8266MQTTMesh::Builder(networks, mqtt_server, mqtt_port)
                       .setVersion(FIRMWARE_VER, FIRMWARE_ID)
                       .setMeshPassword(mesh_password)
#if ASYNC_TCP_SSL_ENABLED
                       .setMqttSSL(mqtt_secure, mqtt_fingerprint)
#if MESH_SECURE
                       .setMeshSSL(ssl_cert, ssl_cert_len, ssl_key, ssl_key_len, ssl_fingerprint)
#endif //MESH_SECURE
#endif //ASYNC_TCP_SSL_ENABLED
                       .build();

void callback(const char *topic, const char *msg);



void setup() {

    Serial.begin(115200);
    delay(1000); //This is only here to make it easier to catch the startup messages.  It isn't required
    mesh.setCallback(callback);
    mesh.begin();
    pinMode(LED_PIN, OUTPUT);

}


void loop() {


    if (! mesh.connected())
        return;

    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {

          String cntStr = String(cnt);
          String msg = "hello from " + ID + " cnt: " + cntStr;
          mesh.publish(ID.c_str(), msg.c_str());
          previousMillis = currentMillis;
          cnt++;

    }

}



void callback(const char *topic, const char *msg) {


    if (0 == strcmp(topic, (const char*) ID.c_str())) {
      if(String(msg) == "0") {
        digitalWrite(LED_PIN, HIGH);
      }else{
        digitalWrite(LED_PIN, LOW);
      }
    }
}
