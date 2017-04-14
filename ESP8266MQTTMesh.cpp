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

#include <limits.h>
extern "C" {
  #include "user_interface.h"
}

size_t strlcat (char *dst, const char *src, size_t len) {
    size_t slen = strlen(dst);
    return strlcpy(dst + slen, src, len - slen);
}

ESP8266MQTTMesh::ESP8266MQTTMesh(const char **networks, const char *network_password, const char *mesh_password,
                                 const char *base_ssid, const char *mqtt_server, int mqtt_port, int mesh_port,
                                 const char *inTopic,   const char *outTopic) :
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
        Serial.println("Max inTopicLen == 16");
        die();
    }
    if (strlen(outTopic) > 16) {
        Serial.println("Max outTopicLen == 16");
        die();
    }
    mySSID[0] = 0;
}

void ESP8266MQTTMesh::setCallback(std::function<void(String topic, String msg)> _callback) {
    callback = _callback;
}
void ESP8266MQTTMesh::setup() {
    Serial.println("Starting");
    if (! SPIFFS.begin()) {
      SPIFFS.format();
      Serial.println("Formatting FS");
      if (! SPIFFS.begin()) {
        Serial.println("Failed to format FS");
        while(1);
      }
    }
    Dir dir = SPIFFS.openDir("/bssid/");
    while(dir.next()) {
      Serial.println(" ==> '" + dir.fileName() + "'");
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
  
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqtt_callback(topic, payload, length); });
    Serial.print(WiFi.status());
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
           Serial.println("WiFi Connection Failed");
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
               Serial.println("MQTT Connection Failed");
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
    Serial.println("Trying to match known BSSIDs for " + String(bssid));
    strlcpy(filename, "/bssid/", sizeof(filename));
    strlcat(filename, bssid, sizeof(filename));
    return SPIFFS.exists(filename);
}

void ESP8266MQTTMesh::connect() {
    Serial.print("Scanning for networks\n");
    int numberOfNetworksFound = WiFi.scanNetworks(false,true);
    Serial.print("Found: "); Serial.println(numberOfNetworksFound);
    int best_match = -1;
    long maxRSSI = LONG_MIN;

    for(int i = 0; i < numberOfNetworksFound; i++) {
        bool found = false;
        const char *ssid = WiFi.SSID(i).c_str();
        Serial.println("Found SSID: '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(i) + "'");
        if (ssid[0] != 0) {
            for(int j = 0; networks[j] != NULL && networks[j][0] != 0; j++) {
                if(strcmp(ssid, networks[j]) == 0) {
                    Serial.println("Matched");
                    found = true;
                    break;
                }
            }
            if(! found) {
                Serial.println("Did not match SSID list");
                continue;
            }
            if (0) {
                FSInfo fs_info;
                SPIFFS.info(fs_info);
                if (fs_info.usedBytes !=0) {
                    Serial.println("Trying to match known BSSIDs for " + WiFi.BSSIDstr(i));
                    if (! match_bssid(WiFi.BSSIDstr(i).c_str())) {
                        Serial.println("Failed to match BSSID");
                        continue;
                    }
                }
            }
        } else {
            if (! match_bssid(WiFi.BSSIDstr(i).c_str())) {
                Serial.println("Failed to match BSSID");
                continue;
            }
        }
        Serial.print("RSSI: "); Serial.println(WiFi.RSSI(i));
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
        Serial.println("Connecting to SSID : '" + String(ssid) + "' BSSID '" + WiFi.BSSIDstr(best_match) + "'");
        const char *password = meshConnect ? mesh_password : network_password;
        //WiFi.begin(ssid.c_str(), password.c_str(), 0, WiFi.BSSID(best_match), true);
        WiFi.begin(ssid, password);
        for (int i = 0; i < 120 && ! connected(); i++) {
            delay(500);
            Serial.println(WiFi.status());
            //Serial.print(".");
        }
        if (connected()) {
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            if (match_bssid(WiFi.softAPmacAddress().c_str())) {
                setup_AP();
            }
        }
    }
}

void ESP8266MQTTMesh::parse_message(const char *topic, const char *msg) {
  int inTopicLen = strlen(inTopic);
  if (strstr(topic, inTopic) != 0) {
      return;
  }
  if (strstr(topic + inTopicLen,"bssid/") == 0) {
      const char *bssid = topic + inTopicLen + 6;
      char filename[32];
      strlcpy(filename, "/bssid/", sizeof(filename));
      strlcat(filename, bssid, sizeof(filename));
      if(SPIFFS.exists(filename)) {
          return;
      }
      File f = SPIFFS.open(filename, "w");
      if (! f) {
          Serial.println("Failed to write /" + String(bssid));
          return;
      }
      f.print(msg);
      f.print("\n");
      f.close();
      return;
  }
  else if (strstr(topic + inTopicLen,"ota/") == 0) {
#if HAS_OTA
      const char *cmd = topic + inTopicLen + 4;
      handle_ota(cmd, msg);
#endif
      return;
  }
  if (! callback) {
      return;
  }
  int mySSIDLen = strlen(mySSID);
  if(strstr(topic + inTopicLen, mySSID) == 0) {
      //Only handle messages addressed to this node
      callback(topic + inTopicLen + mySSIDLen, msg);
  }
  else if(strstr(topic + inTopicLen, "broadcast/") == 0) {
      //Or messages sent to all nodes
      callback(topic + inTopicLen + 10, msg);
  }
}

void ESP8266MQTTMesh::mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);
  parse_message(topic, msg.c_str());
  broadcast_message(String(topic) + "=" + msg);
}

void ESP8266MQTTMesh::connect_mqtt() {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(String("ESP8266-" + WiFi.softAPmacAddress()).c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      char publishMsg[TOPIC_LEN];
      strlcpy(publishMsg, outTopic, sizeof(publishMsg));
      strlcat(publishMsg, WiFi.localIP().toString().c_str(), sizeof(publishMsg));
      mqttClient.publish(publishMsg, "connected");
      // ... and resubscribe
      char subscribe[TOPIC_LEN];
      strlcpy(subscribe, inTopic, sizeof(subscribe));
      strlcat(subscribe, "#", sizeof(subscribe));
      mqttClient.subscribe(subscribe);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
    }
}

void ESP8266MQTTMesh::publish(String subtopic, String msg) {
    char topic[64];
    char req[MQTT_MAX_PACKET_SIZE];
    strlcpy(req, outTopic, sizeof(req));
    strlcat(req, mySSID, sizeof(req));
    strlcat(req, subtopic.c_str(), sizeof(req));
    if (meshConnect) {
        // Send message through mesh network
        strlcat(req, "=", sizeof(req));
        strlcat(req, msg.c_str(), sizeof(req));
        send_message(WiFi.gatewayIP(), req);
    } else {
        mqttClient.publish(req, msg.c_str());
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
    Serial.println("Initialized AP as '" + String(mySSID) + "'  IP '" + apIP.toString() + "'");
    strlcat(mySSID, "/", sizeof(mySSID));
    espServer.begin();
    if (meshConnect) {
        publish("mesh_cmd", "request_bssid");
    }
    AP_ready = true;
}
void ESP8266MQTTMesh::read_until(File &f, char *buf, char delim, int len) {
      int bytes = f.read((uint8_t *)buf, len);
      for (int i = 0; i < len; i++) {
          if (buf[i] == delim) {
              buf[i] = 0;
              f.seek(i-bytes, SeekCur);;
              break;
          }
      }
}
int ESP8266MQTTMesh::read_subdomain(const char *fileName) {
      char subdomain[4];
      File f = SPIFFS.open(fileName, "r");
      if (! f) {
          Serial.println("Failed to read " + String(fileName));
          return -1;
      }
      read_until(f, subdomain, '\n', sizeof(subdomain));
      f.close();
      int value = atoi(subdomain);
      if (value < 0 || value > 255) {
          Serial.print("Illegal value '" + String(subdomain) + "' from " + String(fileName));
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
      Serial.println("Mapping " + dir.fileName() + " to " + String(value) + " ");
      seen[value] = 1;
    }
    for (int i = 4; i < 256; i++) {
        if (! seen[i]) {
            File f = SPIFFS.open("/bssid/" +  WiFi.softAPmacAddress(), "w");
            if (! f) {
                Serial.println("Couldn't write "  + WiFi.softAPmacAddress());
                die();
            }
            f.print(i);
            f.print("\n");
            f.close();
            //Yes this is meant to be inTopic.  That allows all other nodes to see this message
            char topic[TOPIC_LEN];
            strlcpy(topic, inTopic, sizeof(topic));
            strlcat(topic, "bssid/", sizeof(topic));
            Serial.println("Publishing " + String(topic) + " == " + String(i));
            mqttClient.publish(topic, String(i).c_str(), true);
            return;
        }
    }
    die();
}
void ESP8266MQTTMesh::send_message(IPAddress ip, String msg) {
    WiFiClient client;
    if (client.connect(ip, mesh_port)) {
        client.print(msg + "\n");
        client.flush();
        client.stop();
        Serial.println("Sending '" + msg + "' to " + ip.toString());
    } else {
        Serial.println("Failed to send message '" + msg + "' to " + ip.toString());
    }
}

void ESP8266MQTTMesh::broadcast_message(String msg) {
    struct station_info *station_list = wifi_softap_get_station_info();
    while (station_list != NULL) {
        char station_mac[18] = {0}; sprintf(station_mac, "%02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(station_list->bssid));
        IPAddress station_ip = IPAddress((&station_list->ip)->addr);
        Serial.println("Broadcasting to: " + String(station_mac) + " @ " + station_ip.toString());
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
            String req = client.readStringUntil('\n');
            bool isLocal = (client.localIP() == WiFi.localIP() ? true : false);
            Serial.println("Received: msg from " + client.remoteIP().toString() + " on " + (isLocal ? "STA" : "AP"));
            Serial.println("1: " + client.localIP().toString() + " 2: " + WiFi.localIP().toString() + " 3: " + WiFi.softAPIP().toString());
            Serial.println("--> '" + req + "'");
            client.flush();
            client.stop();
            String topic, msg;
            keyValue(req, '=', topic, msg);
            if (isLocal) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                parse_message(topic.c_str(), msg.c_str());
                broadcast_message(req);
            } else {
                if (topic.endsWith("/mesh_cmd")) {
                    // We will handle this packet locally
                    if (msg == "request_bssid") {
                        send_bssids(client.localIP());
                    }
                } else {
                    if (meshConnect) {
                        send_message(WiFi.gatewayIP(), req);
                    } else {
                        mqttClient.publish(topic.c_str(), msg.c_str(), false);
                    }
                }
            }
        }
    }
}

void ESP8266MQTTMesh::keyValue(const String data, char separator, String &key, String &value) {
  int maxIndex = data.length()-1;
  int strIndex = -1;
  for(int i=0; i<=maxIndex; i++) {
      if (data.charAt(i) == separator) {
          strIndex = i;
          break;
      }
  }
  if (strIndex == -1) {
      key = data;
      value = "";
  } else {
      key = data.substring(0, strIndex);
      value = data.substring(strIndex+1);
  }
}

void ESP8266MQTTMesh::handle_ota(const String cmd, const String msg) {
/*
    if(cmd == "start") {
        String kv, unparsed;
        unparsed = msg;
        while(unparsed.length()) {
            keyValue(msg, ',', kv, unparsed);
            String key, value;
            keyValue(kv,':', key, value);
            if (key == "id") {
              id = value.
        int len = 
        for int i = 0; i < 
        StaticJsonBuffer<128> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(msg);

const char* sensor = root["sensor"];
long time          = root["time"];
double latitude    = root["data"][0];
double longitude   = root["data"][1];
    }
*/
}
