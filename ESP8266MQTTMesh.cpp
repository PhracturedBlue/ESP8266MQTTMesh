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
 */

#include "ESP8266MQTTMesh.h"
#include "Base64.h"
#include "eboot_command.h"

#include <limits.h>
extern "C" {
  #include "user_interface.h"
  extern uint32_t _SPIFFS_start;
}


//#define DEBUG_LEVEL (DEBUG_WIFI | DEBUG_MQTT | DEBUG_OTA)
#define DEBUG_LEVEL DEBUG_ALL


#define dbgPrint(lvl, msg) if (((lvl) & (DEBUG_LEVEL)) == (lvl)) Serial.print(msg)
#define dbgPrintln(lvl, msg) if (((lvl) & (DEBUG_LEVEL)) == (lvl)) Serial.println(msg)
size_t strlcat (char *dst, const char *src, size_t len) {
    size_t slen = strlen(dst);
    return strlcpy(dst + slen, src, len - slen);
}

ESP8266MQTTMesh::ESP8266MQTTMesh(unsigned int firmware_id, const char *firmware_ver,
                                 const char **networks, const char *network_password, const char *mesh_password,
                                 const char *base_ssid, const char *mqtt_server, int mqtt_port, int mesh_port,
                                 const char *inTopic,   const char *outTopic) :
        firmware_id(firmware_id),
        firmware_ver(firmware_ver),
        networks(networks),
        network_password(network_password),
        mesh_password(mesh_password),
        base_ssid(base_ssid),
        mqtt_server(mqtt_server),
        mqtt_port(mqtt_port),
        mesh_port(mesh_port),
        inTopic(inTopic),
        outTopic(outTopic),
        espServer(mqtt_port),
        mqttClient(espClient)
{
    if (strlen(inTopic) > 16) {
        dbgPrintln(DEBUG_MSG, "Max inTopicLen == 16");
        die();
    }
    if (strlen(outTopic) > 16) {
        dbgPrintln(DEBUG_MSG, "Max outTopicLen == 16");
        die();
    }
    mySSID[0] = 0;
#if HAS_OTA
    uint32_t usedSize = ESP.getSketchSize();
    // round one sector up
    freeSpaceStart = (usedSize + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
    //freeSpaceEnd = (uint32_t)&_SPIFFS_start - 0x40200000;
    freeSpaceEnd = ESP.getFreeSketchSpace() + freeSpaceStart;
#endif
}

void ESP8266MQTTMesh::setCallback(std::function<void(const char *topic, const char *msg)> _callback) {
    callback = _callback;
}
void ESP8266MQTTMesh::setup() {
    dbgPrintln(DEBUG_MSG_EXTRA, "Starting Firmware" + String(firmware_id, HEX) + " : " + String(firmware_ver));
#if HAS_OTA
    dbgPrintln(DEBUG_MSG_EXTRA, "OTA Start: 0x" + String(freeSpaceStart, HEX) + " OTA End: 0x" + String(freeSpaceEnd, HEX));
#endif
    if (! SPIFFS.begin()) {
      dbgPrintln(DEBUG_MSG_EXTRA, "Formatting FS");
      SPIFFS.format();
      if (! SPIFFS.begin()) {
        dbgPrintln(DEBUG_MSG, "Failed to format FS");
        die();
      }
    }
Serial.println("1");
    Dir dir = SPIFFS.openDir("/bssid/");
Serial.println("2");
    while(dir.next()) {
      Serial.println(" ==> '" + dir.fileName() + "'");
    }
Serial.println("3");
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
  
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqtt_callback(topic, payload, length); });
    Serial.print(WiFi.status());
    dbgPrintln(DEBUG_MSG_EXTRA, "Setup Complete");
}

void ESP8266MQTTMesh::loop() {
    if (!connected()) {
       WiFi.softAPdisconnect();
       if (millis() - lastReconnect < 5000) {
           return;
       }
       connect();
       lastReconnect = millis();
       if (! connected()) {
           dbgPrintln(DEBUG_MSG, "WiFi Connection Failed");
           return;
       }
    }
    if (! meshConnect) {
        // We will connect directly to the MQTT broker
        if(! mqttClient.connected()) {
           if (millis() - lastReconnect < 5000) {
               return;
           }
           connect_mqtt();
           lastReconnect = millis();
           if (! mqttClient.connected()) {
               dbgPrintln(DEBUG_MSG, "MQTT Connection Failed");
               return;
           }
        }
        mqttClient.loop();
        long now = millis();
        if (! AP_ready) {
            if (match_bssid(WiFi.softAPmacAddress().c_str())) {
                setup_AP();
            } else if (now - lastReconnect > 10000) {
                assign_subdomain();
                setup_AP();
            } else {
                return;
            }
        }
    }
    WiFiClient client = espServer.available();
    if (client) {
        handle_client_connection(client);
    }
}

bool ESP8266MQTTMesh::match_bssid(const char *bssid) {
    char filename[32];
    dbgPrintln(DEBUG_WIFI, "Trying to match known BSSIDs for " + String(bssid));
    strlcpy(filename, "/bssid/", sizeof(filename));
    strlcat(filename, bssid, sizeof(filename));
    return SPIFFS.exists(filename);
}

void ESP8266MQTTMesh::connect() {
    dbgPrintln(DEBUG_WIFI, "Scanning for networks");
    int numberOfNetworksFound = WiFi.scanNetworks(false,true);
    dbgPrintln(DEBUG_WIFI, "Found: " + String(numberOfNetworksFound));
    int best_match = -1;
    long maxRSSI = LONG_MIN;

    for(int i = 0; i < numberOfNetworksFound; i++) {
        bool found = false;
        char ssid[32];
        strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
        dbgPrintln(DEBUG_WIFI, "Found SSID: '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(i) + "'");
        if (ssid[0] != 0) {
            for(int j = 0; networks[j] != NULL && networks[j][0] != 0; j++) {
                if(strcmp(ssid, networks[j]) == 0) {
                    dbgPrintln(DEBUG_WIFI, "Matched");
                    found = true;
                    break;
                }
            }
            if(! found) {
                dbgPrintln(DEBUG_WIFI, "Did not match SSID list");
                continue;
            }
            if (0) {
                FSInfo fs_info;
                SPIFFS.info(fs_info);
                if (fs_info.usedBytes !=0) {
                    dbgPrintln(DEBUG_WIFI, "Trying to match known BSSIDs for " + WiFi.BSSIDstr(i));
                    if (! match_bssid(WiFi.BSSIDstr(i).c_str())) {
                        dbgPrintln(DEBUG_WIFI, "Failed to match BSSID");
                        continue;
                    }
                }
            }
        } else {
            if (! match_bssid(WiFi.BSSIDstr(i).c_str())) {
                dbgPrintln(DEBUG_WIFI, "Failed to match BSSID");
                continue;
            }
        }
        dbgPrintln(DEBUG_WIFI, "RSSI: " + String(WiFi.RSSI(i)));
        if (WiFi.RSSI(i) > maxRSSI) {
            maxRSSI = WiFi.RSSI(i);
            best_match = i;
        }
    }
    if (best_match != -1) {
        char ssid[64];
        if (WiFi.SSID(best_match) == "") {
            char subdomain_c[8];
            char filename[32];
            strlcpy(filename, "/bssid/", sizeof(filename));
            strlcat(filename, WiFi.BSSIDstr(best_match).c_str(), sizeof(filename));
            int subdomain = read_subdomain(filename);
            if (subdomain == -1) {
                return;
            }
            itoa(subdomain, subdomain_c, 10);
            strlcpy(ssid, base_ssid, sizeof(ssid));
            strlcat(ssid, subdomain_c, sizeof(ssid));
            meshConnect = true;
        } else {
            strlcpy(ssid, WiFi.SSID(best_match).c_str(), sizeof(ssid));
            meshConnect = false;
        }
        dbgPrintln(DEBUG_WIFI, "Connecting to SSID : '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(best_match) + "'");
        const char *password = meshConnect ? mesh_password : network_password;
        //WiFi.begin(ssid.c_str(), password.c_str(), 0, WiFi.BSSID(best_match), true);
        WiFi.begin(ssid, password);
        for (int i = 0; i < 120 && ! connected(); i++) {
            delay(500);
            dbgPrintln(DEBUG_WIFI, WiFi.status());
            //Serial.print(".");
        }
        if (connected()) {
            dbgPrintln(DEBUG_WIFI, "WiFi connected");
            dbgPrintln(DEBUG_WIFI, "IP address: ");
            dbgPrintln(DEBUG_WIFI, WiFi.localIP());
            if (match_bssid(WiFi.softAPmacAddress().c_str())) {
                setup_AP();
            }
        }
    }
}

void ESP8266MQTTMesh::parse_message(const char *topic, const char *msg) {
  int inTopicLen = strlen(inTopic);
  if (strstr(topic, inTopic) != topic) {
      return;
  }
  const char *subtopic = topic + inTopicLen;
  if (strstr(subtopic,"bssid/") == subtopic) {
      const char *bssid = subtopic + 6;
      char filename[32];
      strlcpy(filename, "/bssid/", sizeof(filename));
      strlcat(filename, bssid, sizeof(filename));
      if(SPIFFS.exists(filename)) {
          return;
      }
      File f = SPIFFS.open(filename, "w");
      if (! f) {
          dbgPrintln(DEBUG_MQTT, "Failed to write /" + String(bssid));
          return;
      }
      f.print(msg);
      f.print("\n");
      f.close();
      return;
  }
  else if (strstr(subtopic ,"ota/") == subtopic) {
#if HAS_OTA
      const char *cmd = subtopic + 4;
      handle_ota(cmd, msg);
#endif
      return;
  }
  if (! callback) {
      return;
  }
  int mySSIDLen = strlen(mySSID);
  if(strstr(subtopic, mySSID) == subtopic) {
      //Only handle messages addressed to this node
      callback(subtopic + mySSIDLen, msg);
  }
  else if(strstr(subtopic, "broadcast/") == subtopic) {
      //Or messages sent to all nodes
      callback(subtopic + 10, msg);
  }
}

void ESP8266MQTTMesh::mqtt_callback(const char* topic, const byte* payload, unsigned int length) {
  dbgPrint(DEBUG_MQTT, "Message arrived [");
  dbgPrint(DEBUG_MQTT, topic);
  dbgPrint(DEBUG_MQTT, "] ");
  int len = strlen(topic);
  strlcpy(buffer, topic, sizeof(buffer));
  buffer[len++] = '=';
  strlcpy(buffer + len, (char *)payload, sizeof(buffer)-len > length ? length+1 : sizeof(buffer)-len);
  dbgPrintln(DEBUG_MQTT, buffer);
  broadcast_message(buffer);
  parse_message(topic, buffer+len);
}

void ESP8266MQTTMesh::connect_mqtt() {
    dbgPrintln(DEBUG_MQTT, "Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(String("ESP8266-" + WiFi.softAPmacAddress()).c_str())) {
      dbgPrintln(DEBUG_MQTT, "connected");
      // Once connected, publish an announcement...
      char msg[64];
      char id[9];
      itoa(firmware_id, id, 16);
      strlcpy(msg, "Connected FW: ", sizeof(msg));
      strlcat(msg, id, sizeof(msg));
      strlcat(msg, " : ", sizeof(msg));
      strlcat(msg, firmware_ver, sizeof(msg));
      //strlcpy(publishMsg, outTopic, sizeof(publishMsg));
      //strlcat(publishMsg, WiFi.localIP().toString().c_str(), sizeof(publishMsg));
      publish("connect", msg);
      // ... and resubscribe
      char subscribe[TOPIC_LEN];
      strlcpy(subscribe, inTopic, sizeof(subscribe));
      strlcat(subscribe, "#", sizeof(subscribe));
      mqttClient.subscribe(subscribe);
    } else {
      dbgPrint(DEBUG_MQTT, "failed, rc=");
      dbgPrint(DEBUG_MQTT, mqttClient.state());
      dbgPrintln(DEBUG_MQTT, " try again in 5 seconds");
      // Wait 5 seconds before retrying
    }
}

void ESP8266MQTTMesh::publish(const char *subtopic, const char *msg) {
    char topic[64];
    strlcpy(topic, outTopic, sizeof(topic));
    strlcat(topic, mySSID, sizeof(topic));
    strlcat(topic, subtopic, sizeof(topic));
    dbgPrintln(DEBUG_MQTT_EXTRA, "Sending: " + String(topic) + "=" + String(msg));
    if (meshConnect) {
        // Send message through mesh network
        send_message(WiFi.gatewayIP(), topic, msg);
    } else {
        mqttClient.publish(topic, msg);
    }
}

void ESP8266MQTTMesh::setup_AP() {
    char filename[32];
    strlcpy(filename, "/bssid/", sizeof(filename));
    strlcat(filename, WiFi.softAPmacAddress().c_str(), sizeof(filename));
    int subdomain = read_subdomain(filename);
    if (subdomain == -1) {
        return;
    }
    char subdomainStr[4];
    itoa(subdomain, subdomainStr, 10);
    strlcpy(mySSID, base_ssid, sizeof(mySSID));
    strlcat(mySSID, subdomainStr, sizeof(mySSID));
    IPAddress apIP(192, 168, subdomain, 1);
    IPAddress apGateway(192, 168, subdomain, 1);
    IPAddress apSubmask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGateway, apSubmask);
    WiFi.softAP(mySSID, mesh_password, 1, 1);
    dbgPrintln(DEBUG_WIFI, "Initialized AP as '" + String(mySSID) + "'  IP '" + apIP.toString() + "'");
    strlcat(mySSID, "/", sizeof(mySSID));
    espServer.begin();
    if (meshConnect) {
        publish("mesh_cmd", "request_bssid");
    }
    AP_ready = true;
}
int ESP8266MQTTMesh::read_subdomain(const char *fileName) {
      char subdomain[4];
      File f = SPIFFS.open(fileName, "r");
      if (! f) {
          dbgPrintln(DEBUG_WIFI, "Failed to read " + String(fileName));
          return -1;
      }
      subdomain[f.readBytesUntil('\n', subdomain, sizeof(subdomain)-1)] = 0;
      f.close();
      unsigned int value = strtoul(subdomain, NULL, 10);
      if (value < 0 || value > 255) {
          dbgPrintln(DEBUG_WIFI, "Illegal value '" + String(subdomain) + "' from " + String(fileName));
          return -1;
      }
      return value;
}
void ESP8266MQTTMesh::assign_subdomain() {
    char seen[256];
    memset(seen, 0, sizeof(seen));
    Dir dir = SPIFFS.openDir("/bssid/");
    while(dir.next()) {
      int value = read_subdomain(dir.fileName().c_str());
      if (value == -1) {
          continue;
      }
      dbgPrintln(DEBUG_MQTT, "Mapping " + dir.fileName() + " to " + String(value) + " ");
      seen[value] = 1;
    }
    for (int i = 4; i < 256; i++) {
        if (! seen[i]) {
            File f = SPIFFS.open("/bssid/" +  WiFi.softAPmacAddress(), "w");
            if (! f) {
                dbgPrintln(DEBUG_WIFI, "Couldn't write "  + WiFi.softAPmacAddress());
                die();
            }
            f.print(i);
            f.print("\n");
            f.close();
            //Yes this is meant to be inTopic.  That allows all other nodes to see this message
            char topic[TOPIC_LEN];
            char msg[4];
            itoa(i, msg, 10);
            strlcpy(topic, inTopic, sizeof(topic));
            strlcat(topic, "bssid/", sizeof(topic));
            dbgPrintln(DEBUG_MQTT, "Publishing " + String(topic) + " == " + String(i));
            mqttClient.publish(topic, msg, true);
            return;
        }
    }
    die();
}
void ESP8266MQTTMesh::send_message(IPAddress ip, const char *topicOrMsg, const char *msg) {
    WiFiClient client;
    if (client.connect(ip, mesh_port)) {
        client.print(topicOrMsg);
        if (msg) {
            client.print("=");
            client.print(msg);
        }
        client.print("\n");
        client.flush();
        client.stop();
        dbgPrintln(DEBUG_MQTT, "Sent '" + String(topicOrMsg) + (msg ? String("=" + String(msg)) : "") + "' to " + ip.toString());
    } else {
        dbgPrintln(DEBUG_MQTT, "Failed to send message '" + String(msg) + "' to " + ip.toString());
    }
}

void ESP8266MQTTMesh::broadcast_message(const char *msg) {
    struct station_info *station_list = wifi_softap_get_station_info();
    while (station_list != NULL) {
        char station_mac[18] = {0}; sprintf(station_mac, "%02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(station_list->bssid));
        IPAddress station_ip = IPAddress((&station_list->ip)->addr);
        dbgPrintln(DEBUG_MQTT, "Broadcasting to: " + String(station_mac) + " @ " + station_ip.toString());
        send_message(station_ip, msg);
        station_list = STAILQ_NEXT(station_list, next);
    }
}

void ESP8266MQTTMesh::send_bssids(IPAddress ip) {
    Dir dir = SPIFFS.openDir("/bssid/");
    char msg[TOPIC_LEN+32];
    char subdomainStr[4];
    while(dir.next()) {
        int subdomain = read_subdomain(dir.fileName().c_str());
        if (subdomain == -1) {
            continue;
        }
        itoa(subdomain, subdomainStr, 10);
        strlcpy(msg, inTopic, sizeof(msg));
        strlcat(msg, "bssid/", sizeof(msg));
        strlcat(msg, dir.fileName().substring(7).c_str(), sizeof(msg)); // bssid
        strlcat(msg, "=", sizeof(msg));
        strlcat(msg, subdomainStr, sizeof(msg));
        send_message(ip, msg);
    }
}


void ESP8266MQTTMesh::handle_client_connection(WiFiClient client) {
    while (client.connected()) {
        if (client.available()) {
            buffer[client.readBytesUntil('\n', buffer, sizeof(buffer)-1)] = 0;
            bool isLocal = (client.localIP() == WiFi.localIP() ? true : false);
            dbgPrintln(DEBUG_MQTT, "Received: msg from " + client.remoteIP().toString() + " on " + (isLocal ? "STA" : "AP"));
            dbgPrintln(DEBUG_MQTT_EXTRA, "1: " + client.localIP().toString() + " 2: " + WiFi.localIP().toString() + " 3: " + WiFi.softAPIP().toString());
            dbgPrint(DEBUG_MQTT_EXTRA, "--> '");
            dbgPrint(DEBUG_MQTT_EXTRA, buffer);
            dbgPrintln(DEBUG_MQTT_EXTRA, "'");
            client.flush();
            client.stop();
            char topic[64];
            const char *msg;
            keyValue(buffer, '=', topic, sizeof(topic), &msg);
            if (isLocal) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                broadcast_message(buffer);
                parse_message(topic, msg);
            } else {
                if (strstr(topic,"/mesh_cmd")  == topic + strlen(topic) - 9) {
                    // We will handle this packet locally
                    if (0 == strcmp(msg, "request_bssid")) {
                        send_bssids(client.localIP());
                    }
                } else {
                    if (meshConnect) {
                        send_message(WiFi.gatewayIP(), buffer);
                    } else {
                        mqttClient.publish(topic, msg, false);
                    }
                }
            }
        }
    }
}

bool ESP8266MQTTMesh::keyValue(const char *data, char separator, char *key, int keylen, const char **value) {
  int maxIndex = strlen(data-1);
  int i;
  for(i=0; i<=maxIndex && i <keylen-1; i++) {
      key[i] = data[i];
      if (key[i] == separator) {
          *value = data+i+1;
          key[i] = 0;
          return true;
      }
  }
  key[i] = 0;
  *value = NULL;
  return false;
}

ota_info_t ESP8266MQTTMesh::parse_ota_info(const char *str) {
    ota_info_t ota_info;
    memset (&ota_info, 0, sizeof(ota_info));
    char kv[64];
    while(str) {
        keyValue(str, ',', kv, sizeof(kv), &str);
        dbgPrintln(DEBUG_OTA_EXTRA, "Key/Value: " + String(kv));
        char key[32];
        const char *value;
        if (! keyValue(kv, ':', key, sizeof(key), &value)) {
            dbgPrintln(DEBUG_OTA, "Failed to parse Key/Value: " + String(kv));
            continue;
        }
        dbgPrintln(DEBUG_OTA_EXTRA, "Key: " + String(key) + " Value: " + String(value));
        if (0 == strcmp(key, "len")) {
            ota_info.len = strtoul(value, NULL, 10);
        } else if (0 == strcmp(key, "md5")) {
            if(strlen(value) == 24 && base64_dec_len(value, 24) == 16) {
              base64_decode((char *)ota_info.md5, value,  24);
            } else {
              dbgPrintln(DEBUG_OTA, "Failed to parse md5");
            }
        }
    }
    return ota_info;
}
bool ESP8266MQTTMesh::check_ota_md5() {
    uint8_t buf[128];
    File f = SPIFFS.open("/ota", "r");
    buf[f.readBytesUntil('\n', buf, sizeof(buf)-1)] = 0;
    f.close();
    dbgPrintln(DEBUG_OTA_EXTRA, "Read /ota: " + String((char *)buf));
    ota_info_t ota_info = parse_ota_info((char *)buf);
    if (ota_info.len > freeSpaceEnd - freeSpaceStart) {
        return false;
    }
    MD5Builder _md5;
    _md5.begin();
    uint32_t address = freeSpaceStart;
    unsigned int len = ota_info.len;
    while(len) {
        int size = len > sizeof(buf) ? sizeof(buf) : len;
        if (! ESP.flashRead(address, (uint32_t *)buf, (size + 3) & ~3)) {
            return false;
        }
        _md5.add(buf, size);
        address += size;
        len -= size;
    }
    _md5.calculate();
    _md5.getBytes(buf);
    for (int i = 0; i < 16; i++) {
        if (buf[i] != ota_info.md5[i]) {
            return false;
        }
    }
    return true;
}

void ESP8266MQTTMesh::handle_ota(const char *cmd, const char *msg) {
    char *end;
    unsigned int id = strtoul(cmd,&end, 16);

    dbgPrintln(DEBUG_OTA_EXTRA, "OTA cmd " + String(cmd) + " Length: " + String(strlen(msg)));
    if (id != firmware_id || *end != '/') {
        dbgPrintln(DEBUG_OTA, "Ignoring OTA because firmwareID did not match " + String(firmware_id, HEX));
        return;
    }
    cmd += (end - cmd) + 1; //skip ID
    if(0 == strcmp(cmd, "start")) {
        dbgPrintln(DEBUG_OTA_EXTRA, "OTA Start");
        ota_info_t ota_info = parse_ota_info(msg);
        if (ota_info.len == 0) {
            dbgPrintln(DEBUG_OTA, "Ignoring OTA because firmware length = 0");
            return;
        }
        dbgPrintln(DEBUG_OTA, "-> " + String(msg));
        File f = SPIFFS.open("/ota", "w");
        f.print(msg);
        f.print("\n");
        f.close();
        f = SPIFFS.open("/ota", "r");
        char buf[128];
        buf[f.readBytesUntil('\n', buf, sizeof(buf)-1)] = 0;
        f.close();
        dbgPrintln(DEBUG_OTA, "--> " + String(buf));
        if (ota_info.len > freeSpaceEnd - freeSpaceStart) {
            dbgPrintln(DEBUG_MSG, "Not enough space for firmware: " + String(ota_info.len) + " > " + String(freeSpaceEnd - freeSpaceStart));
            return;
        }
        int end = (freeSpaceStart + ota_info.len + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
        //erase flash area here
        dbgPrintln(DEBUG_OTA, "Erasing " + String((end - freeSpaceStart)/ FLASH_SECTOR_SIZE) + " sectors");
        long t = micros();
        for (int i = freeSpaceStart / FLASH_SECTOR_SIZE; i < end / FLASH_SECTOR_SIZE; i++) {
           ESP.flashEraseSector(i);
           yield();
        }
        dbgPrintln(DEBUG_OTA, "Erase complete in " +  String((micros() - t) / 1000000.0, 6) + " seconds");
    }
    else if(0 == strcmp(cmd, "check")) {
        if (strlen(msg) > 0) {
            char out[33];
            MD5Builder _md5;
            _md5.begin();
            _md5.add((uint8_t *)msg, strlen(msg));
            _md5.calculate();
            _md5.getChars(out);
            publish("check", out);
        } else {
            const char *md5ok = check_ota_md5() ? "MD5 Passed" : "MD5 Failed";
            dbgPrintln(DEBUG_OTA, md5ok);
            publish("check", md5ok);
        }
    }
    else if(0 == strcmp(cmd, "flash")) {
        if (! check_ota_md5()) {
            dbgPrintln(DEBUG_MSG, "Flash failed due to md5 mismatch");
            publish("flash", "Failed");
            return;
        }
        uint8_t buf[128];
        File f = SPIFFS.open("/ota", "r");
        buf[f.readBytesUntil('\n', buf, sizeof(buf)-1)] = 0;
        ota_info_t ota_info = parse_ota_info((char *)buf);
        dbgPrintln(DEBUG_OTA, "Flashing");
        
        eboot_command ebcmd;
        ebcmd.action = ACTION_COPY_RAW;
        ebcmd.args[0] = freeSpaceStart;
        ebcmd.args[1] = 0x00000;
        ebcmd.args[2] = ota_info.len;
        eboot_command_write(&ebcmd);
        //publish("flash", "Success");
        ESP.restart();
    }
    else {
        char *end;
        unsigned int address = strtoul(cmd, &end, 10);
        if (address > freeSpaceEnd - freeSpaceStart || end != cmd + strlen(cmd)) {
            dbgPrintln(DEBUG_MSG, "Illegal address " + String(address) + " specified");
            return;
        }
        int msglen = strlen(msg);
        if (msglen > 1024) {
            dbgPrintln(DEBUG_MSG, "Message length " + String(msglen) + " too long");
            return;
        }
        byte data[768];
        long t = micros();
        int len = base64_decode((char *)data, msg, msglen);
        if (address + len > freeSpaceEnd) {
            dbgPrintln(DEBUG_MSG, "Message length would run past end of free space");
            return;
        }
        dbgPrintln(DEBUG_OTA_EXTRA, "Got " + String(len) + " bytes FW @ " + String(address, HEX));
        bool ok = ESP.flashWrite(freeSpaceStart + address, (uint32_t*) data, len);
        dbgPrintln(DEBUG_OTA, "Wrote " + String(len) + " bytes in " +  String((micros() - t) / 1000000.0, 6) + " seconds");
        yield();
        if (! ok) {
            dbgPrintln(DEBUG_MSG, "Failed to write firmware at " + String(freeSpaceStart + address, HEX) + " Length: " + String(len));
        }
    }
}
