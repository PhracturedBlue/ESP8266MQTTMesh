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

//Define GATEWAY_ID to the value of ESP.getChipId() in order to prevent only a specific node from connecting via MQTT
#ifdef GATEWAY_ID
    #define IS_GATEWAY (ESP.getChipId() == GATEWAY_ID)
#else
    #define IS_GATEWAY (1)
#endif

#ifndef STAILQ_NEXT //HAS_ESP8266_24
#define NEXT_STATION(station_list)  station_list->next
#else
#define NEXT_STATION(station_list) STAILQ_NEXT(station_list, next)
#endif

//#define DEBUG_LEVEL (DEBUG_WIFI | DEBUG_MQTT | DEBUG_OTA)
#define DEBUG_LEVEL DEBUG_ALL_EXTRA


#define dbgPrintln(lvl, msg) if (((lvl) & (DEBUG_LEVEL)) == (lvl)) Serial.println("[" + String(__FUNCTION__) + "] " + msg)
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
        espServer(mesh_port)
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
    espClient[0] = new AsyncClient();
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
      dbgPrintln(DEBUG_FS, " ==> '" + dir.fileName() + "'");
    }
    WiFi.disconnect();
    // In the ESP8266 2.3.0 API, there seems to be a bug which prevents a node configured as
    // WIFI_AP_STA from openning a TCP connection to it's gateway if the gateway is also
    // in WIFI_AP_STA
    WiFi.mode(WIFI_STA);

    wifiConnectHandler     = WiFi.onStationModeGotIP(            [this] (const WiFiEventStationModeGotIP& e) {                this->onWifiConnect(e);    }); 
    wifiDisconnectHandler  = WiFi.onStationModeDisconnected(     [this] (const WiFiEventStationModeDisconnected& e) {         this->onWifiDisconnect(e); });
    //wifiDHCPTimeoutHandler = WiFi.onStationModeDHCPTimeout(      [this] () {                                                  this->onDHCPTimeout();     });
    wifiAPConnectHandler   = WiFi.onSoftAPModeStationConnected(  [this] (const WiFiEventSoftAPModeStationConnected& ip) {     this->onAPConnect(ip);     });
    wifiAPDisconnectHandler= WiFi.onSoftAPModeStationDisconnected([this] (const WiFiEventSoftAPModeStationDisconnected& ip) { this->onAPDisconnect(ip);  });

    espClient[0]->setNoDelay(true);
    espClient[0]->onConnect(   [this](void * arg, AsyncClient *c)                           { this->onConnect(c);         }, this);
    espClient[0]->onDisconnect([this](void * arg, AsyncClient *c)                           { this->onDisconnect(c);      }, this);
    espClient[0]->onError(     [this](void * arg, AsyncClient *c, int8_t error)             { this->onError(c, error);    }, this);
    espClient[0]->onAck(       [this](void * arg, AsyncClient *c, size_t len, uint32_t time){ this->onAck(c, len, time);  }, this);
    espClient[0]->onTimeout(   [this](void * arg, AsyncClient *c, uint32_t time)            { this->onTimeout(c, time);   }, this);
    espClient[0]->onData(      [this](void * arg, AsyncClient *c, void* data, size_t len)   { this->onData(c, data, len); }, this);

    espServer.onClient(     [this](void * arg, AsyncClient *c){ this->onClient(c);  }, this);
    espServer.setNoDelay(true);
    espServer.begin();

    mqttClient.onConnect(    [this] (bool sessionPresent)                    { this->onMqttConnect(sessionPresent); });
    mqttClient.onDisconnect( [this] (AsyncMqttClientDisconnectReason reason) { this->onMqttDisconnect(reason); });
    mqttClient.onSubscribe(  [this] (uint16_t packetId, uint8_t qos)         { this->onMqttSubscribe(packetId, qos); });
    mqttClient.onUnsubscribe([this] (uint16_t packetId)                      { this->onMqttUnsubscribe(packetId); });
    mqttClient.onMessage(    [this] (char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
                                                                             { this->onMqttMessage(topic, payload, properties, len, index, total); });
    mqttClient.onPublish(    [this] (uint16_t packetId)                      { this->onMqttPublish(packetId); });
    mqttClient.setServer(mqtt_server, mqtt_port);
    //mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqtt_callback(topic, payload, length); });


    dbgPrintln(DEBUG_WIFI_EXTRA, WiFi.status());
    dbgPrintln(DEBUG_MSG_EXTRA, "Setup Complete");
    ap_idx = LAST_AP;
    connect();
}

bool ESP8266MQTTMesh::isAPConnected(uint8 *mac) {
    struct station_info *station_list = wifi_softap_get_station_info();
    while (station_list != NULL) {
        if(memcmp(mac, station_list->bssid, 6) == 0) {
            return true;
        }
        station_list = NEXT_STATION(station_list);
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
        station_list = NEXT_STATION(station_list);
    }
    memset(mac, 0, 6);
}

bool ESP8266MQTTMesh::connected() {
    return wifiConnected() && ((meshConnect && espClient[0] && espClient[0]->connected()) || mqttClient.connected());
}

bool ESP8266MQTTMesh::match_bssid(const char *bssid) {
    char filename[32];
    dbgPrintln(DEBUG_WIFI, "Trying to match known BSSIDs for " + String(bssid));
    strlcpy(filename, "/bssid/", sizeof(filename));
    strlcat(filename, bssid, sizeof(filename));
    return SPIFFS.exists(filename);
}

void ESP8266MQTTMesh::scan() {
    //Need to rescan
    if (! scanning) {
        for(int i = 0; i < LAST_AP; i++) {
            ap[i].rssi = -99999;
            ap[i].ssid_idx = NETWORK_LAST_INDEX;
        }
        ap_idx = 0;
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        dbgPrintln(DEBUG_WIFI, "Scanning for networks");
        WiFi.scanDelete();
        WiFi.scanNetworks(true,true);
        scanning = true;
    }
    int numberOfNetworksFound = WiFi.scanComplete();
    if (numberOfNetworksFound < 0) {
        return;
    }
    scanning = false;
    dbgPrintln(DEBUG_WIFI, "Found: " + String(numberOfNetworksFound));
    int ssid_idx;
    for(int i = 0; i < numberOfNetworksFound; i++) {
        bool found = false;
        char ssid[32];
        int network_idx = NETWORK_MESH_NODE;
        strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
        dbgPrintln(DEBUG_WIFI, "Found SSID: '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(i) + "'");
        if (ssid[0] != 0) {
            if (IS_GATEWAY) {
            for(network_idx = 0; networks[network_idx] != NULL && networks[network_idx][0] != 0; network_idx++) {
                if(strcmp(ssid, networks[network_idx]) == 0) {
                    dbgPrintln(DEBUG_WIFI, "Matched");
                    found = true;
                    break;
                }
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
        for(int j = 0; j < LAST_AP; j++) {
            if(ap[j].ssid_idx == NETWORK_LAST_INDEX ||
               (network_idx >= 0 &&
                  (ap[j].ssid_idx == NETWORK_MESH_NODE || rssi > ap[j].rssi)) ||
               (network_idx == NETWORK_MESH_NODE && ap[j].ssid_idx == NETWORK_MESH_NODE && rssi > ap[j].rssi))
            {
                for(int k = LAST_AP -1; k > j; k--) {
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

void ESP8266MQTTMesh::schedule_connect(float delay) {
    dbgPrintln(DEBUG_WIFI, "Scheduling reconnect for " + String(delay,2)+ " seconds from now");
    schedule.once(delay, connect, this);
}

void ESP8266MQTTMesh::connect() {
    if (WiFi.isConnected()) {
        dbgPrintln(DEBUG_WIFI, "Called connect when already connected!");
        return;
    }
    connecting = false;
    retry_connect = 1;
    lastReconnect = millis();
    if (scanning || ap_idx >= LAST_AP ||  ap[ap_idx].ssid_idx == NETWORK_LAST_INDEX) {
        scan();
    } if (scanning) {
        schedule_connect(0.5);
        return;
    }
    if (ap[ap_idx].ssid_idx == NETWORK_LAST_INDEX) {
        // No networks found, try again
        schedule_connect();
        return;
    }    
    for (int i = 0; i < LAST_AP; i++) {
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
            ap_idx++;
            schedule_connect();
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
      int idx = strtoul(msg, NULL, 10);
      int subdomain = read_subdomain(filename);
      if (subdomain == idx) {
          // The new value matches the stored value
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

      if (strcmp(WiFi.softAPmacAddress().c_str(), bssid) == 0) {
          shutdown_AP();
          setup_AP();
      }
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


void ESP8266MQTTMesh::connect_mqtt() {
    dbgPrintln(DEBUG_MQTT, "Attempting MQTT connection...");
    // Attempt to connect
    mqttClient.connect();
}


void ESP8266MQTTMesh::publish(const char *subtopic, const char *msg) {
    char topic[64];
    strlcpy(topic, outTopic, sizeof(topic));
    strlcat(topic, mySSID, sizeof(topic));
    strlcat(topic, subtopic, sizeof(topic));
    dbgPrintln(DEBUG_MQTT_EXTRA, "Sending: " + String(topic) + "=" + String(msg));
    if (! meshConnect) {
        mqttClient.publish(topic, 0, false, msg);
    } else {
        send_message(0, topic, msg);
    }
}

void ESP8266MQTTMesh::shutdown_AP() {
    if(! AP_ready)
        return;
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if(espClient[i]) {
            delete espClient[i];
            espClient[i] = NULL;
        }
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    AP_ready = false;
}

void ESP8266MQTTMesh::setup_AP() {
    if (AP_ready)
        return;
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
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apGateway, apSubmask);
    WiFi.softAP(mySSID, mesh_password, WiFi.channel(), 1);
    dbgPrintln(DEBUG_WIFI, "Initialized AP as '" + String(mySSID) + "'  IP '" + apIP.toString() + "'");
    strlcat(mySSID, "/", sizeof(mySSID));
    if (meshConnect) {
        publish("mesh_cmd", "request_bssid");
    }
    connecting = false; //Connection complete
    AP_ready = true;
}
int ESP8266MQTTMesh::read_subdomain(const char *fileName) {
      char subdomain[4];
      File f = SPIFFS.open(fileName, "r");
      if (! f) {
          dbgPrintln(DEBUG_MSG_EXTRA, "Failed to read " + String(fileName));
          return -1;
      }
      subdomain[f.readBytesUntil('\n', subdomain, sizeof(subdomain)-1)] = 0;
      f.close();
      unsigned int value = strtoul(subdomain, NULL, 10);
      if (value < 0 || value > 255) {
          dbgPrintln(DEBUG_MSG, "Illegal value '" + String(subdomain) + "' from " + String(fileName));
          return -1;
      }
      return value;
}
void ESP8266MQTTMesh::assign_subdomain() {
    char seen[256];
    if (match_bssid(WiFi.softAPmacAddress().c_str())) {
        return;
    }
    memset(seen, 0, sizeof(seen));
    Dir dir = SPIFFS.openDir("/bssid/");
    while(dir.next()) {
      int value = read_subdomain(dir.fileName().c_str());
      if (value == -1) {
          continue;
      }
      dbgPrintln(DEBUG_WIFI_EXTRA, "Mapping " + dir.fileName() + " to " + String(value) + " ");
      seen[value] = 1;
    }
    for (int i = 4; i < 256; i++) {
        if (! seen[i]) {
            File f = SPIFFS.open("/bssid/" +  WiFi.softAPmacAddress(), "w");
            if (! f) {
                dbgPrintln(DEBUG_MSG, "Couldn't write "  + WiFi.softAPmacAddress());
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
            dbgPrintln(DEBUG_MQTT_EXTRA, "Publishing " + String(topic) + " == " + String(i));
            mqttClient.publish(topic, 0, true, msg);
            setup_AP();
            return;
        }
    }
}

bool ESP8266MQTTMesh::send_message(int index, const char *topicOrMsg, const char *msg) {
    int topicLen = strlen(topicOrMsg);
    int msgLen = 0;
    int len = topicLen;
    espClient[index]->write(topicOrMsg);
    if (msg) {
        espClient[index]->write("=", 1);
        espClient[index]->write(msg);
    }
    espClient[index]->write("\0", 1);
    return true;
}

void ESP8266MQTTMesh::broadcast_message(const char *topicOrMsg, const char *msg) {
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (espClient[i]) {
            send_message(i, topicOrMsg, msg);
        }
    }
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
        send_message(idx, msg);
    }
}


void ESP8266MQTTMesh::handle_client_data(int idx, char *data) {
            dbgPrintln(DEBUG_MQTT, "Received: msg from " + espClient[idx]->remoteIP().toString() + " on " + (idx == 0 ? "STA" : "AP"));
            dbgPrintln(DEBUG_MQTT_EXTRA, "--> '" + String(data) + "'");
            char topic[64];
            const char *msg;
            if (! keyValue(data, '=', topic, sizeof(topic), &msg)) {
                dbgPrintln(DEBUG_MQTT, "Failed to handle message");
                return;
            }
            if (idx == 0) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                broadcast_message(data);
                parse_message(topic, msg);
            } else {
                if (strstr(topic,"/mesh_cmd")  == topic + strlen(topic) - 9) {
                    // We will handle this packet locally
                    if (0 == strcmp(msg, "request_bssid")) {
                        send_bssids(idx);
                    }
                } else {
                    if (! meshConnect) {
                        mqttClient.publish(topic, 0, false, msg);
                    } else {
                        send_message(0, data);
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

        shutdown_AP();
        mqttClient.disconnect();
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

void ESP8266MQTTMesh::onWifiConnect(const WiFiEventStationModeGotIP& event) {
    if (meshConnect) {
        dbgPrintln(DEBUG_WIFI, "Connecting to mesh: " + WiFi.gatewayIP().toString() + " on port: " + String(mesh_port));
        espClient[0]->connect(WiFi.gatewayIP(), mesh_port);
        bufptr[0] = inbuffer[0];
    } else {
        dbgPrintln(DEBUG_WIFI, "Connecting to mqtt");
        connect_mqtt();
    }
}

void ESP8266MQTTMesh::onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
    //Reasons are here: ESP8266WiFiType.h-> WiFiDisconnectReason 
    dbgPrintln(DEBUG_WIFI, "Disconnected from Wi-Fi: " + event.ssid + " because: " + String(event.reason));
    WiFi.disconnect();
    if (! connecting) {
        ap_idx = LAST_AP;
    } else if (event.reason == WIFI_DISCONNECT_REASON_ASSOC_TOOMANY  && retry_connect) {
        // If we rebooted without a clean shutdown, we may still be associated with this AP, in which case
        // we'll be booted and should try again
        retry_connect--;
    } else {
        ap_idx++;
    }
    schedule_connect();
}

//void ESP8266MQTTMesh::onDHCPTimeout() {
//    dbgPrintln(DEBUG_WIFI, "Failed to get DHCP info");
//}

void ESP8266MQTTMesh::onAPConnect(const WiFiEventSoftAPModeStationConnected& ip) {
    dbgPrintln(DEBUG_WIFI, "Got connection from Station");
}

void ESP8266MQTTMesh::onAPDisconnect(const WiFiEventSoftAPModeStationDisconnected& ip) {
    dbgPrintln(DEBUG_WIFI, "Got disconnection from Station");
}

void ESP8266MQTTMesh::onMqttConnect(bool sessionPresent) {
    dbgPrintln(DEBUG_MQTT, "MQTT Connected");
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
    mqttClient.publish("connect", 0, false, msg);
    // ... and resubscribe
    char subscribe[TOPIC_LEN];
    strlcpy(subscribe, inTopic, sizeof(subscribe));
    strlcat(subscribe, "#", sizeof(subscribe));
    mqttClient.subscribe(subscribe, 0);

    if (match_bssid(WiFi.softAPmacAddress().c_str())) {
        setup_AP();
    } else {
        //If we don't get a mapping for our BSSID within 10 seconds, define one
        schedule.once(10.0, assign_subdomain, this);
    }
}

void ESP8266MQTTMesh::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    dbgPrintln(DEBUG_MQTT, "Disconnected from MQTT.");
    shutdown_AP();
    if (WiFi.isConnected()) {
        connect_mqtt();
    }
}

void ESP8266MQTTMesh::onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void ESP8266MQTTMesh::onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void ESP8266MQTTMesh::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  memcpy(inbuffer[0], payload, len);
  inbuffer[0][len]= 0;
  dbgPrintln(DEBUG_MQTT_EXTRA, "Message arrived [" + String(topic) + "] '" + String(inbuffer[0]) + "'");
  broadcast_message(topic, inbuffer[0]);
  parse_message(topic, inbuffer[0]);
}

void ESP8266MQTTMesh::onMqttPublish(uint16_t packetId) {
  //Serial.println("Publish acknowledged.");
  //Serial.print("  packetId: ");
  //Serial.println(packetId);
}

void ESP8266MQTTMesh::onClient(AsyncClient* c) {
    dbgPrintln(DEBUG_WIFI, "Got client connection from: " + c->remoteIP().toString());
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (! espClient[i]) {
            espClient[i] = c;
            espClient[i]->onDisconnect([this](void * arg, AsyncClient *c)                           { this->onDisconnect(c);      }, this);
            espClient[i]->onError(     [this](void * arg, AsyncClient *c, int8_t error)             { this->onError(c, error);    }, this);
            espClient[i]->onAck(       [this](void * arg, AsyncClient *c, size_t len, uint32_t time){ this->onAck(c, len, time);  }, this);
            espClient[i]->onTimeout(   [this](void * arg, AsyncClient *c, uint32_t time)            { this->onTimeout(c, time);   }, this);
            espClient[i]->onData(      [this](void * arg, AsyncClient *c, void* data, size_t len)   { this->onData(c, data, len); }, this);
            bufptr[i] = inbuffer[i];
            return;
        }
    }
    dbgPrintln(DEBUG_WIFI, "Discarding client connection from: " + c->remoteIP().toString());
    delete c;
}

void ESP8266MQTTMesh::onConnect(AsyncClient* c) {
    dbgPrintln(DEBUG_WIFI, "Connected to mesh");
    if (match_bssid(WiFi.softAPmacAddress().c_str())) {
        setup_AP();
    }
}

void ESP8266MQTTMesh::onDisconnect(AsyncClient* c) {
    if (c == espClient[0]) {
        dbgPrintln(DEBUG_WIFI, "Disconnected from mesh");
        shutdown_AP();
        WiFi.disconnect();
        return;
    }
    for (int i = 1; i <= ESP8266_NUM_CLIENTS; i++) {
        if (c == espClient[i]) {
            dbgPrintln(DEBUG_WIFI, "Disconnected from AP");
            delete espClient[i];
            espClient[i] = NULL;
        }
    }
    dbgPrintln(DEBUG_WIFI, "Disconnected unknown client");
}
void ESP8266MQTTMesh::onError(AsyncClient* c, int8_t error) {
    dbgPrintln(DEBUG_WIFI, "Got error on " + c->remoteIP().toString() + ": " + String(error));
}
void ESP8266MQTTMesh::onAck(AsyncClient* c, size_t len, uint32_t time) {
    dbgPrintln(DEBUG_WIFI_EXTRA, "Got ack on " + c->remoteIP().toString() + ": " + String(len) + " / " + String(time));
}

void ESP8266MQTTMesh::onTimeout(AsyncClient* c, uint32_t time) {
    dbgPrintln(DEBUG_WIFI, "Got timeout  " + c->remoteIP().toString() + ": " + String(time));
    c->close();
}

void ESP8266MQTTMesh::onData(AsyncClient* c, void* data, size_t len) {
    dbgPrintln(DEBUG_WIFI_EXTRA, "Got data from " + c->remoteIP().toString());
    for (int idx = meshConnect ? 0 : 1; idx <= ESP8266_NUM_CLIENTS; idx++) {
        if (espClient[idx] == c) {
            char *dptr = (char *)data;
            for (int i = 0; i < len; i++) {
                *bufptr[idx]++ = dptr[i];
                if(! dptr[i]) {
                    handle_client_data(idx, inbuffer[idx]);
                    bufptr[idx] = inbuffer[idx];
                }
            }
            return;
        }
    }
    dbgPrintln(DEBUG_WIFI, "Could not find client");
}
