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

enum {
    NETWORK_LAST_INDEX = -2,
    NETWORK_MESH_NODE  = -1,
};

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
        espServer(mesh_port),
        mqttClient(espMQTTClient),
        ringBuf((MQTT_MAX_PACKET_SIZE + 3)*10)

{
    int len = strlen(inTopic);
    if (len > 16) {
        dbgPrintln(DEBUG_MSG, "Max inTopicLen == 16");
        die();
    }
    if (inTopic[len-1] != '/') {
        dbgPrintln(DEBUG_MSG, "inTopic must end with '/'");
        die();
    }
    len = strlen(outTopic);
    if (len > 16) {
        dbgPrintln(DEBUG_MSG, "Max outTopicLen == 16");
        die();
    }
    if (outTopic[len-1] != '/') {
        dbgPrintln(DEBUG_MSG, "outTopic must end with '/'");
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
void ESP8266MQTTMesh::begin() {
    dbgPrintln(DEBUG_MSG_EXTRA, "Starting Firmware " + String(firmware_id, HEX) + " : " + String(firmware_ver));
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
    Dir dir = SPIFFS.openDir("/bssid/");
    while(dir.next()) {
      Serial.println(" ==> '" + dir.fileName() + "'");
    }
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
  
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqtt_callback(topic, payload, length); });
    Serial.print(WiFi.status());
    dbgPrintln(DEBUG_MSG_EXTRA, "Setup Complete");
}

void ESP8266MQTTMesh::loop() {
    if (!wifiConnected() && ! connecting) {
       WiFi.softAPdisconnect(true);
       WiFi.disconnect();
       if (millis() - lastReconnect < 5000) {
           return;
       }
       scan();
       connect();
    }
    if (connecting) {
       if (wifiConnected()) {
            connecting = false;
            dbgPrintln(DEBUG_WIFI, "WiFi connected");
            dbgPrintln(DEBUG_WIFI, "IP address: ");
            dbgPrintln(DEBUG_WIFI, WiFi.localIP());
            if (meshConnect) {
                if (espClient[0].connect(WiFi.gatewayIP(), mesh_port)) {
                    espClient[0].setNoDelay(true);
                } else {
                    // AP rejected our connetcion...Try another AP
                    dbgPrintln(DEBUG_MSG, "AP Rejected connection");
                    lastReconnect = millis();
                    WiFi.disconnect();
                    ap_idx++;
                    connect();
                    return;
                }
            }
            if (match_bssid(WiFi.softAPmacAddress().c_str())) {
                setup_AP();
            }
        } else {
            if (millis() - lastStatus > 500) {
                dbgPrintln(DEBUG_WIFI, WiFi.status());
                lastStatus = millis();
            }
            if (millis() - lastReconnect > 60000) {
                if (! wifiConnected()) {
                    dbgPrintln(DEBUG_MSG, "WiFi Connection Failed");
                }
                lastReconnect = millis();
                WiFi.disconnect();
                ap_idx++;
                connect();
            }
        }
    }
    if (! wifiConnected()) {
        return;
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
    while (espServer.hasClient())  {
        int i;
        uint8 mac[6];
        WiFiClient c = espServer.available();
        getMAC(c.remoteIP(), mac);
        for (i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
            bool unused = false;
            if(! espClient[i] || ! espClient[i].connected()
               || memcmp(espMAC[i], mac, 6) == 0
               || ! isAPConnected(espMAC[i]))
            {
                if(espClient[i])
                    espClient[i].stop();
                espClient[i] = c;
                espClient[i].setNoDelay(true);
                memcpy(espMAC[i], mac, 6);
                dbgPrintln(DEBUG_WIFI,"Assigning IP " + espClient[i].localIP().toString() + " to idx: " + String(i));
                break;
            }            
        }
        if (i == ESP8266_NUM_CLIENTS + 1) {
            c.flush();
            c.stop();
        }
    }
    send_messages();
    for (int i = meshConnect ? 0 : 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (espClient[i] && espClient[i].connected()) {
            int strIdx = strlen(readData[i]);
            while (espClient[i].available()) {
                unsigned char c = espClient[i].read();
                if (c == '\r')
                    continue;
                if (c == '\n') {
                    handle_client_data(i);
                    strIdx = 0;
                    readData[i][0] = 0;
                    continue;
                }
                if (strIdx == sizeof(readData[i])) {
                    dbgPrintln(DEBUG_MQTT, "Ran out of space in buffer " + String(i) + " " + String(readData[i]));
                    strIdx = 0;
                    readData[i][0] = 0;
                    continue;
                }
                readData[i][strIdx++] = c;
                readData[i][strIdx] = 0;
            }
        }
    }
}

bool ESP8266MQTTMesh::isAPConnected(uint8 *mac) {
    struct station_info *station_list = wifi_softap_get_station_info();
    while (station_list != NULL) {
        if(memcmp(mac, station_list->bssid, 6) == 0) {
            return true;
        }
        station_list = station_list->next;
    }
    return false;
}

void ESP8266MQTTMesh::getMAC(IPAddress ip, uint8 *mac) {
    struct station_info *station_list = wifi_softap_get_station_info();
    while (station_list != NULL) {
        if ((&station_list->ip)->addr == ip) {
            memcpy(mac, station_list->bssid, 6);
            return;
        }
        station_list = station_list->next;
    }
    memset(mac, 0, 6);
}

bool ESP8266MQTTMesh::connected() {
    return wifiConnected() && (meshConnect || mqttClient.connected());
}

bool ESP8266MQTTMesh::match_bssid(const char *bssid) {
    char filename[32];
    dbgPrintln(DEBUG_WIFI, "Trying to match known BSSIDs for " + String(bssid));
    strlcpy(filename, "/bssid/", sizeof(filename));
    strlcat(filename, bssid, sizeof(filename));
    return SPIFFS.exists(filename);
}

void ESP8266MQTTMesh::scan() {
    for(int i = 0; i < sizeof(ap) / sizeof(ap_t); i++) {
        ap[i].rssi = -99999;
        ap[i].ssid_idx = -2;
    }
    ap_idx = 0;
    dbgPrintln(DEBUG_WIFI, "Scanning for networks");
    int numberOfNetworksFound = WiFi.scanNetworks(false,true);
    dbgPrintln(DEBUG_WIFI, "Found: " + String(numberOfNetworksFound));
    int ssid_idx;
    for(int i = 0; i < numberOfNetworksFound; i++) {
        bool found = false;
        char ssid[32];
        int network_idx = NETWORK_MESH_NODE;
        strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
        dbgPrintln(DEBUG_WIFI, "Found SSID: '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(i) + "'");
        if (ssid[0] != 0) {
            for(network_idx = 0; networks[network_idx] != NULL && networks[network_idx][0] != 0; network_idx++) {
                if(strcmp(ssid, networks[network_idx]) == 0) {
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
        int rssi = WiFi.RSSI(i);
        //sort by RSSI
        for(int j = 0; j < sizeof(ap) / sizeof(ap_t); j++) {
            if(ap[j].ssid_idx == NETWORK_LAST_INDEX ||
               (network_idx >= 0 &&
                  (ap[j].ssid_idx == NETWORK_MESH_NODE || rssi > ap[j].rssi)) ||
               (network_idx == NETWORK_MESH_NODE && ap[j].ssid_idx == NETWORK_MESH_NODE && rssi > ap[j].rssi))
            {
                for(int k = sizeof(ap) / sizeof(ap_t) -1; k > j; k--) {
                    ap[k] = ap[k-1];
                }
                ap[j].rssi = rssi;
                ap[j].ssid_idx = network_idx;
                strlcpy(ap[j].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(ap[j].bssid));
                break;
            }
        }
    }
}

void ESP8266MQTTMesh::connect() {
    connecting = false;
    lastReconnect = millis();
    if (ap_idx >= sizeof(ap)/sizeof(ap_t) ||  ap[ap_idx].ssid_idx == NETWORK_LAST_INDEX) {
        return;
    }
    for (int i = 0; i < sizeof(ap)/sizeof(ap_t); i++) {
        dbgPrintln(DEBUG_WIFI, String(i) + String(i == ap_idx ? " * " : "   ") + String(ap[i].bssid) + " " + String(ap[i].rssi));
    }
    char ssid[64];
    if (ap[ap_idx].ssid_idx == NETWORK_MESH_NODE) {
        //This is a mesh node
        char subdomain_c[8];
        char filename[32];
        strlcpy(filename, "/bssid/", sizeof(filename));
        strlcat(filename, ap[ap_idx].bssid, sizeof(filename));
        int subdomain = read_subdomain(filename);
        if (subdomain == -1) {
            return;
        }
        itoa(subdomain, subdomain_c, 10);
        strlcpy(ssid, base_ssid, sizeof(ssid));
        strlcat(ssid, subdomain_c, sizeof(ssid));
        meshConnect = true;
    } else {
        strlcpy(ssid, networks[ap[ap_idx].ssid_idx], sizeof(ssid));
        meshConnect = false;
    }
    dbgPrintln(DEBUG_WIFI, "Connecting to SSID : '" + String(ssid) + "' BSSID '" + String(ap[ap_idx].bssid) + "'");
    const char *password = meshConnect ? mesh_password : network_password;
    //WiFi.begin(ssid.c_str(), password.c_str(), 0, WiFi.BSSID(best_match), true);
    WiFi.begin(ssid, password);
    connecting = true;
    lastStatus = lastReconnect;
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
  queue_message(-1, buffer);
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
        queue_message(0, topic, msg);
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
            strlcat(topic, WiFi.softAPmacAddress().c_str(), sizeof(topic));
            dbgPrintln(DEBUG_MQTT, "Publishing " + String(topic) + " == " + String(i));
            mqttClient.publish(topic, msg, true);
            return;
        }
    }
    die();
}
bool ESP8266MQTTMesh::queue_message(int index, const char *topicOrMsg, const char *msg) {
    int topicLen = strlen(topicOrMsg);
    int msgLen = 0;
    int len = topicLen;
    if (msg) {
        msgLen = strlen(msg);
        len += 1 + msgLen;
    }
    int available = ringBuf.room();
    if (len + 3 > available) {
        dbgPrintln(DEBUG_MQTT, "Not enough bytes: " + String(len+3) + " > " + String(available) + " to send: '" + String(topicOrMsg) + (msg ? String("=" + String(msg)) : "") + "'");
        return false;
        
    }
    ringBuf.write((char)index);
    ringBuf.write((char)(len >> 8));
    ringBuf.write((char)(len  & 0xff));
    ringBuf.write(topicOrMsg, topicLen);
    if (msg) {
        ringBuf.write('=');
        ringBuf.write(msg, msgLen);
    }
    dbgPrintln(DEBUG_MQTT, "Queued '" + String(topicOrMsg) + (msg ? String("=" + String(msg)) : "") + "' to " + (index == -1 ? "Broadcast" : espClient[index].remoteIP().toString()));
    return true;
}

void ESP8266MQTTMesh::send_messages() {
    if (ringBuf.empty())
        return;
    char tmp[3];
    ringBuf.peek(tmp, 3);
    int index = (signed char)tmp[0];
    int len = tmp[1] * 256 + tmp[2];
    int pos = 0;
    int start_Index = (index == -1) ? 1 : index;
    if (send_InProgress) {
        if (index == -1) {
            start_Index = send_CurrentIdx;
        }
        pos = send_Pos;
    }
    int last_Index = (index == -1) ? ESP8266_NUM_CLIENTS : index;
    ringBuf.peek(buffer, 3 + len);
    buffer[len+3] = '\n';
    buffer[len+4] = 0;
    len++;
    for(int i = start_Index; i <= last_Index; i++, pos = 0) {
        if(! espClient[i] || ! espClient[i].connected()) {
            continue;
        }
        int avail = espClient[i].availableForWrite();
        int size  = len < avail ? len : avail;
        dbgPrintln(DEBUG_MQTT, String(index == -1 ? "Broadcasting" : "Sending") + " to " + espClient[i].remoteIP().toString() + "@" + String(i));
        int sent = espClient[i].write(buffer+3 + pos, len - pos);
        if (sent == 0) {
            espClient[i].stop();
        }
        if (size != len) {
            //We didn't write the whole buffer
            send_InProgress = true;
            send_CurrentIdx = i;
            send_Pos = size;
            return;
        }
    }
    ringBuf.remove(len + 3 - 1);
    send_InProgress = false;
}

void ESP8266MQTTMesh::send_bssids(int idx) {
    Dir dir = SPIFFS.openDir("/bssid/");
    char msg[TOPIC_LEN];
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
        queue_message(idx, msg);
    }
}


void ESP8266MQTTMesh::handle_client_data(int idx) {
            dbgPrintln(DEBUG_MQTT, "Received: msg from " + espClient[idx].remoteIP().toString() + " on " + (idx == 0 ? "STA" : "AP"));
            dbgPrintln(DEBUG_MQTT_EXTRA, "1: " + espClient[idx].localIP().toString() + " 2: " + WiFi.localIP().toString() + " 3: " + WiFi.softAPIP().toString());
            dbgPrint(DEBUG_MQTT_EXTRA, "--> '");
            dbgPrint(DEBUG_MQTT_EXTRA, readData[idx]);
            dbgPrintln(DEBUG_MQTT_EXTRA, "'");
            char topic[64];
            const char *msg;
            keyValue(readData[idx], '=', topic, sizeof(topic), &msg);
            if (idx == 0) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                queue_message(-1, readData[idx]);
                parse_message(topic, msg);
            } else {
                if (strstr(topic,"/mesh_cmd")  == topic + strlen(topic) - 9) {
                    // We will handle this packet locally
                    if (0 == strcmp(msg, "request_bssid")) {
                        send_bssids(idx);
                    }
                } else {
                    if (meshConnect) {
                        // Send message through mesh network
                        queue_message(0, readData[idx]);
                    } else {
                        mqttClient.publish(topic, msg, false);
                    }
                }
            }
}

bool ESP8266MQTTMesh::keyValue(const char *data, char separator, char *key, int keylen, const char **value) {
  int maxIndex = strlen(data)-1;
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

        WiFi.softAPdisconnect();
        mqttClient.disconnect();
        espServer.stop();
        delay(100);
        ESP.restart();
        die();
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
