#include "ESP8266MQTTMesh.h"
#include "Utils.h"

#include <limits.h>
extern "C" {
  #include "user_interface.h"
}
#include "credentials.h"
/* credentials.h should include the following:
   const String networks[] = {
             "network ssid1",
             "network ssid2",
             "",
             };
   const char* network_password = "ssid password";
   const char* mesh_password    = "mesh password";
   const String base_ssid       = "mesh_esp8266-";
   const char* mqtt_server      = "MQTT server IP address";
   const int   mqtt_port        = 1883;
*/


ESP8266MQTTMesh::ESP8266MQTTMesh():
        espServer(mqtt_port),
        mqttClient(espClient)
{
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
    mqttClient.setCallback(callback);
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
            if (match_bssid(WiFi.softAPmacAddress())) {
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
    //if (now - lastMsg > 2000) {
    //  lastMsg = now;
    //  ++value;
    //  snprintf (msg, 75, "hello world #%ld", value);
    //  Serial.print("Publish message: ");
    //  Serial.println(msg);
    //  mqttClient.publish("outTopic", msg);
    //}
}

bool ESP8266MQTTMesh::match_bssid(String bssid) {
    Serial.println("Trying to match known BSSIDs for " + bssid);
    return SPIFFS.exists("/bssid/"+ bssid);
}

void ESP8266MQTTMesh::connect() {
    Serial.print("Scanning for newtorks\n");
    int numberOfNetworksFound = WiFi.scanNetworks(false,true);
    Serial.print("Found: "); Serial.println(numberOfNetworksFound);
    int best_match = -1;
    long maxRSSI = LONG_MIN;

    for(int i = 0; i < numberOfNetworksFound; i++) {
        bool found = false;
        String ssid = WiFi.SSID(i);
        Serial.println("Found SSID: '" + ssid + "' BSSID '" + WiFi.BSSIDstr(i) + "'");
        if (ssid != "") {
            for(int j = 0; networks[j] != ""; j++) {
                if(ssid == networks[j]) {
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
                    if (! match_bssid(WiFi.BSSIDstr(i))) {
                        Serial.println("Failed to match BSSID");
                        continue;
                    }
                }
            }
        } else {
            if (! match_bssid(WiFi.BSSIDstr(i))) {
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
        String ssid = WiFi.SSID(best_match);
        if (ssid == "") {
            int subdomain = read_subdomain("/bssid/" + WiFi.BSSIDstr(best_match));
            if (subdomain == -1) {
                return;
            }
            ssid = base_ssid + String(subdomain);
            meshConnect = true;
        } else {
            meshConnect = false;
        }
        Serial.println("Connecting to SSID : '" + ssid + "' BSSID '" + WiFi.BSSIDstr(best_match) + "'");
        String password = meshConnect ? mesh_password : network_password;
        //WiFi.begin(ssid.c_str(), password.c_str(), 0, WiFi.BSSID(best_match), true);
        WiFi.begin(ssid.c_str(), password.c_str());
        for (int i = 0; i < 120 && ! connected(); i++) {
            delay(500);
            Serial.println(WiFi.status());
            //Serial.print(".");
        }
        if (connected()) {
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            if (match_bssid(WiFi.softAPmacAddress())) {
                setup_AP();
            }
        }
    }
}

void ESP8266MQTTMesh::callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);
  if (strncmp(topic, "esp8266/bssid/", 14) == 0) {
      String bssid = &topic[14];
      if(SPIFFS.exists("/bssid/" + bssid)) {
          return;
      }
      File f = SPIFFS.open("/bssid/" + bssid, "w");
      if (! f) {
          Serial.println("Failed to write /" + bssid);
          return;
      }
      f.print(msg);
      f.print("\n");
      f.close();
      return;
  }
  broadcast_message(String(topic) + "=" + msg);
}

void ESP8266MQTTMesh::connect_mqtt() {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(String("ESP8266-" + WiFi.softAPmacAddress()).c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      mqttClient.publish(String("esp8266/" + WiFi.localIP().toString()).c_str(), "connected");
      // ... and resubscribe
      mqttClient.subscribe("esp8266/#");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
    }
}

void ESP8266MQTTMesh::publish(String subtopic, String msg) {
    String topic = "esp8266/" + mySSID + "/" + subtopic;
    if (meshConnect) {
        // Send message through mesh network
        String req = topic + "=" + msg;
        send_message(WiFi.gatewayIP(), req);
    } else {
        mqttClient.publish(topic.c_str(), msg.c_str());
    }
}

void ESP8266MQTTMesh::setup_AP() {
    int subdomain = read_subdomain("/bssid/" + WiFi.softAPmacAddress());
    if (subdomain == -1) {
        return;
    }
    mySSID = base_ssid + String(subdomain);
    IPAddress apIP(192, 168, subdomain, 1);
    IPAddress apGateway(192, 168, subdomain, 1);
    IPAddress apSubmask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGateway, apSubmask);
    WiFi.softAP(mySSID.c_str(), mesh_password, 1, 1);
    Serial.println("Initialized AP as '" + mySSID + "'  IP '" + apIP.toString() + "'");
    espServer.begin();
    if (meshConnect) {
        publish("mesh_cmd", "request_bssid");
    }
    AP_ready = true;
}
int ESP8266MQTTMesh::read_subdomain(String fileName) {
      File f = SPIFFS.open(fileName, "r");
      if (! f) {
          Serial.println("Failed to read " + fileName);
          return -1;
      }
      String subdomain = f.readStringUntil('\n');
      f.close();
      int value = subdomain.toInt();
      if (value < 0 || value > 255) {
          Serial.print("Illegal value '" + subdomain + "' from " + fileName);
          return -1;
      }
      return value;
}
void ESP8266MQTTMesh::assign_subdomain() {
    char seen[256];
    memset(seen, 0, sizeof(seen));
    Dir dir = SPIFFS.openDir("/bssid/");
    while(dir.next()) {
      int value = read_subdomain(dir.fileName());
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
            String topic = "esp8266/bssid/" + WiFi.softAPmacAddress();
            Serial.println("Publishing " + topic + " == " + String(i));
            mqttClient.publish(topic.c_str(), String(i).c_str(), true);
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
    while(dir.next()) {
        String bssid = dir.fileName().substring(7);
        int subdomain = read_subdomain(dir.fileName());
        if (subdomain == -1) {
            continue;
        }
        send_message(ip, "esp8266/bssid/" + bssid + "=" + String(subdomain));
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
            if (isLocal) {
                //This is a packet from MQTT, need to rebroadcast to each connected station
                broadcast_message(msg);
            } else {
                String topic = getValue(req, '=', 0);
                String msg   = getValue(req, '=', 1);
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
