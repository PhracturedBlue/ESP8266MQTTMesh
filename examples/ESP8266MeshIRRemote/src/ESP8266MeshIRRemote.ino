/*
 *  Copyright (C) 2016 PhracturedBlue
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Based on IRremoteESP8266: IRServer - demonstrates sending IR codes controlled from a webserver
 * 
 *  esp8266IR Remote allows Pronto IR codes to be sent via an IR LED connected on GPIO2 via http
 *  It is recommended to drive an n-fet transistor connected to a resistor and diode, since the 
 *  esp8266 can only supply 12mA on its GPIO pin.
 * 
 *  Known commands:
 *  NOTE: The maximum length of topic + payload is ~1156 bytes
 *  <topic>/send : code=<pronto code>                                           : send specified code one time
 *  <topic>/send : repeat=5,code=<pronto code>                                  : send specified code 5 times
 *  <topic>/send : repeat=5,code=<pronto code1>,repeat=3,pronto=<pronto code2>  : send code1 5 times followed by sending code2 3 times
 *  <topic>/send : repeat=5,file=<filename>                                     : send pronto code previously saved in 'filename' 5 times
 *  <topic>/list : ""                                                           : returns a list of all saved files
 *  <topic>/debug: <1|0>                                                        : enable/disable debugging messages over MQTT
 *  <topic>/save/<filename> : code=<pronto code>                                : save specified pronto code to 'filename' on esp8266 device
 *  <topic>/read/<filename> : ""                                                : return contents of 'flename' via MQTT
 *  
 *  <topic> will generally be something like: 'esp8266-in/mesh_esp8266-7'
 *  Version 0.2 2017-04-16
 */

/* See credentials.h.examle for contents of credentials.h */
#include "credentials.h"
#include <ESP8266WiFi.h>
#include <FS.h>

#include <ESP8266MQTTMesh.h>
#include <IRremoteESP8266.h>
#include "QueueArray.h"

#define      FIRMWARE_ID        0x2222
#define      FIRMWARE_VER       "0.2"

const wifi_conn  networks[]   = NETWORK_LIST;
const char*  mqtt_server      = MQTT_SERVER;
const char*  mesh_password    = MESH_PASSWORD;

ESP8266MQTTMesh mesh = ESP8266MQTTMesh::Builder(networks, mqtt_server)
                     .setVersion(FIRMWARE_VER, FIRMWARE_ID)
                     .setMeshPassword(mesh_password)
                     .build();

#define ESP8266_LED 2

bool debug = false;

class Cmd {
  public:
  Cmd(String _c, int _r, String _p, String _d = "") {
    code = _c;
    repeat = _r;
    protocol = _p;
    description = _d;
  }
  String code;
  String protocol;
  String description;
  int repeat;
};
QueueArray<struct Cmd *> cmdQueue(10);

IRsend irsend(ESP8266_LED);

void handleList() {
  String message = "{ \"Commands\": {";
    Dir dir = SPIFFS.openDir("/ir/");
    bool first = true;
    while(dir.next()) {
      if (! first) {
          message += ",";
      }
      File f = dir.openFile("r");
      int size = f.size();
      f.close();
      message += " \"" + dir.fileName().substring(4) + "\": " + String(size);
      first = false;
    }
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    message += " }, \"Free\": " + String(fs_info.totalBytes - fs_info.usedBytes) + "}";
    mesh.publish("list", message.c_str());
}

Cmd *parse_code(const char *msg, bool queue = false) {
    String code = "";
    String protocol = "pronto";
    int repeat = 0;
    bool debug = false;
    bool seen_repeat = false;
    while (msg) {
        char kv[1024];
        char key[16];
        const char *value;
        ESP8266MQTTMesh::keyValue(msg, ',', kv, sizeof(kv), &msg);
        if (! ESP8266MQTTMesh::keyValue(kv, '=', key, sizeof(key), &value)) {
            continue;
        };
        if (0 == strcmp(key, "repeat")) {
            repeat = atoi(value);
            seen_repeat = true;
        }
        else if (0 == strcmp(key, "protocol")) {
            protocol = value;
        }
        else if(0 == strcmp(key, "code")) {
            code = value;
            if (queue) {
                cmdQueue.push(new Cmd(code, repeat, protocol, "Code len: " + String(code.length())));
            }
        }
        else if(0 == strcmp(key, "file")) {
            File f = SPIFFS.open("/ir/" + String(value), "r");
            if (! f) {
                Serial.println("Failed to read file: " + String(value));
                continue;
            }
            protocol = f.readStringUntil('\n');
            code = f.readStringUntil('\n');
            String repeat_str = f.readStringUntil('\n');
            if (repeat_str != "" && !seen_repeat) {
                repeat = repeat_str.toInt();
            }
            if (queue) {
                cmdQueue.push(new Cmd(code, repeat, protocol, "File: " + String(value)));
            }
            f.close();
        }
    }
    if (queue) {
        return NULL;
    }
    Cmd *cmd = new Cmd(code, repeat, protocol);
    return cmd;
}

void callback(const char *topic, const char *msg)
{
    char *endStr;
    if (0 == strcmp(topic, "list")) {
        handleList();
    }
    if (0 == strcmp(topic, "debug")) {
        debug = atoi(msg);
    }
    else if (0 == strcmp(topic, "send")) {
        parse_code(msg, true);
    }
    else if (strstr(topic, "read/") == topic) {
        const char *filename = topic + 5;
        File f = SPIFFS.open("/ir/" + String(filename), "r");
        if (! f) {
            Serial.println("Failed to read file: " + String(filename));
            mesh.publish(topic, "{ \"Failed\": 1 }");
            return;
        }
        String json = "{";
        json += " \"protocol\": \"" + f.readStringUntil('\n') + "\"";
        json += " \"code\": \"" + f.readStringUntil('\n') + "\"";
        json += " \"repeat\": \"" + f.readStringUntil('\n') + "\"";
        json += " }";
        mesh.publish(topic, json.c_str());
    }
    else if (strstr(topic, "save/") == topic) {
        const char *filename = topic + 5;
        Cmd *cmd = parse_code(msg);
        File f = SPIFFS.open("/ir/" + String(filename), "w");
        if (! f) {
            Serial.println("Failed to create file: " + String(filename));
            mesh.publish(topic, "{ \"Failed\": 1 }");
        } else {  
            f.print(cmd->protocol + "\n");
            f.print(cmd->code + "\n");
            f.print(String(cmd->repeat) + "\n");
            f.close();
        }
        delete cmd;
    }
}
void setup(void){
  irsend.begin();
  
  Serial.begin(115200);
  mesh.setCallback(callback);
  mesh.begin();
  Serial.println("");
  if (1) {
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    Serial.print("FS Used: ");
    Serial.print(fs_info.usedBytes);
    Serial.print(" Free: ");
    Serial.print(fs_info.totalBytes - fs_info.usedBytes);
    Serial.print(" Blocksize: ");
    Serial.println(fs_info.blockSize);
    Dir dir = SPIFFS.openDir("/ir/");
    while(dir.next()) {
      Serial.println("File: " + dir.fileName());
    }
  }
}
 
void loop(void){
  if (! cmdQueue.isEmpty()) {
    Cmd *nextCmd = cmdQueue.peek();
    Serial.println("Sendng code: Repeat=" + String(nextCmd->repeat) + " queue size= " + cmdQueue.count());
    if (debug) {
        mesh.publish("tx", String("Queue Len: " + String(cmdQueue.count()) + " Repeat: " + String(nextCmd->repeat) + " Desc: " + nextCmd->description).c_str());
    }
    while (1) {
      irsend.sendPronto(nextCmd->code.c_str(), nextCmd->repeat ? true : false, true);
      if (nextCmd->repeat >= -1) {
        break;
      }
      nextCmd->repeat++;
    }
    //irsend.sendPronto(nextCmd->code.c_str(), nextCmd->repeat ? true : false, true);
    if (nextCmd->repeat <= 1) {
       nextCmd = cmdQueue.pop();
       delete nextCmd;
    } else {
      nextCmd->repeat--;
    }
  }
}
