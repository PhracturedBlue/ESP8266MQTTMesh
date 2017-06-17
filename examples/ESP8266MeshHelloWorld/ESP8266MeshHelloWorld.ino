#include "credentials.h"
#include <ESP8266WiFi.h>
#include <ESP8266MQTTMesh.h>
#include <FS.h>


#if 0
    //DO NOT Remove this section!  It is used to help the makefile find libraries
    #include <AsyncMqttClient.h>
    #include <ESPAsyncTCP.h>
    #include <Ticker.h>
#endif

#ifndef LED_PIN
  #define LED_PIN LED_BUILTIN
#endif


#define      FIRMWARE_ID        0x1337
#define      FIRMWARE_VER       "0.1"
const char*  networks[]       = NETWORK_LIST;
const char*  network_password = NETWORK_PASSWORD;
const char*  mesh_password    = MESH_PASSWORD;
const char*  base_ssid        = BASE_SSID;
const char*  mqtt_server      = MQTT_SERVER;
const int    mqtt_port        = MQTT_PORT;
const int    mesh_port        = MESH_PORT;


String ID  = String(ESP.getChipId());


const char *out_topic = "esp8266-out/";
const char *in_topic = "esp8266-in/";




unsigned long previousMillis = 0;
const long interval = 5000;
int cnt = 0;


ESP8266MQTTMesh mesh(FIRMWARE_ID, FIRMWARE_VER,
                     networks, network_password, mesh_password,
                     base_ssid, mqtt_server, mqtt_port, mesh_port,
                     in_topic, out_topic);


void callback(const char *topic, const char *msg);



void setup() {

    Serial.begin(115200);
    mesh.setCallback(callback);
    mesh.begin();
    pinMode(LED_PIN, OUTPUT);

}


void loop() {


    mesh.loop();
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
