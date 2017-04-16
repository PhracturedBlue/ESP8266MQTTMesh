/*
 * Based on IRremoteESP8266: IRServer - demonstrates sending IR codes controlled from a webserver
 * 
 * esp8266IR Remote allows Pronto IR codes to be sent via an IR LED connected on GPIO2 via http
 * It is recommended to drive an n-fet transistor connected to a resistor and diode, since the 
 * esp8266 can only supply 12mA on its GPIO pin.
 * 
 * Known commands:
 * /ir?pronto=<pronto code>                                           : send specified code one time
 * /ir?repeat=5&pronto=<pronto code>                                  : send specified code 5 times
 * /ir?repeat=5&pronto=<pronto code1>&repeat=3&pronto=<pronto code2>  : send code1 5 times followed by sending code2 3 times
 * /ir?repeat=5&fromfile=<filename>                                   : send pronto code previously saved in 'filename' 5 times
 * /ir?protocol=pronto&data=<pronto code>&savefile=<filename>         : save specified pronto code to 'filename' on esp8266 device
 * 
 * Version 0.1 2015-12-19
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include "QueueArray.h"

#include "FS.h"

#define ESP8266_LED 2
const char* ssid = "audrey";
const char* password = "Audrey is the best!";
bool has_fs;
MDNSResponder mdns;
class Cmd {
  public:
  Cmd(String _c, int _r) {
    code = _c;
    repeat = _r;
  }
  String code;
  int repeat;
};
QueueArray<struct Cmd *> cmdQueue(10);

void urldecode(char *dst, const char *src);

ESP8266WebServer server(80);

IRsend irsend(ESP8266_LED);

void sendMessage(String message) {
 server.send(200, "text/html",
             "<html><head> <title>ESP8266 Demo</title></head>\n"
             "    <body>\n"
             + message +
             "\n    </body>\n"
             "</html>");
}

void handleRoot() {
  sendMessage("");
}

void handleList() {
  String message = "";
    Dir dir = SPIFFS.openDir("/");
    while(dir.next()) {
      message += dir.fileName()+ "<br>\n";
    }
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    message += "Used space: " + String(fs_info.usedBytes) + " Free: " + String(fs_info.totalBytes - fs_info.usedBytes) + "<br>\n";

    sendMessage(message);
}


void handleIr(){
  unsigned long repeat = 0;
  String message = "";
  String data = "";
  String protocol = "pronto";
  bool debug = false;
  for (uint8_t i=0; i<server.args(); i++){
    if (server.argName(i) == "debug") {
      debug = true;
    }
    if (server.argName(i) == "repeat") {
      repeat = server.arg(i).toInt();
    }
    else if(server.argName(i) == "pronto") 
    {
      String code = server.arg(i);
      char * const decoded = new char[code.length()];
      urldecode(decoded, code.c_str());
      if (debug) {
          message += "Repeat: " + String(repeat) + "<br>\n";
          message += "pronto: " + code + "<br>\n";
          Serial.println(decoded);
      }
      cmdQueue.push(new Cmd(String(decoded), repeat));
      delete [] decoded;
    }
    else if(server.argName(i) == "fromfile")
    {
      String filename = server.arg(i);
      File f = SPIFFS.open("/" + filename, "r");
      if (! f) {
        Serial.println("Failed to read file: " + filename);
      } else {
        protocol = f.readStringUntil('\n');
        String data1 = f.readStringUntil('\n');
        f.close();
        if (debug) {
            Serial.println("Read file: " + filename + " protocol: " + protocol);
            Serial.println(data1.c_str());
            message += "Repeat: " + String(repeat) + "<br>\n";
            message += protocol + ": " + data1 + "<br>\n";
        }
        cmdQueue.push(new Cmd(String(data1), repeat));
      }
    }
    else if(server.argName(i) == "data") 
    {
      data = server.arg(i);
    }
    else if(server.argName(i) == "protocol") 
    {
      protocol = server.arg(i);
    }
    else if(server.argName(i) == "savefile" && data != "")
    {
      String filename = server.arg(i);
      File f = SPIFFS.open("/" + filename, "w");
      if (! f) {
        Serial.println("Failed to create file: " + filename);
      } else {  
        f.print(protocol + "\n");
        f.print(data + "\n");
        f.close();
        if (debug) {
            Serial.println("Saved: " + filename);
        }
        data = "";
      }
    }
  }
  sendMessage(message);
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void urldecode(char *dst, const char *src)
{
  char a, b,c;
  if (dst==NULL) return;
  while (*src) {
    if ((*src == '%') &&
      ((a = src[1]) && (b = src[2])) &&
      (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a'-'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a'-'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16*a+b;
      src+=3;
    } 
    else {
        c = *src++;
        if(c=='+')c=' ';
      *dst++ = c;
    }
  }
  *dst++ = '\0';
}

void setup(void){
  irsend.begin();
  
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");
  has_fs = false;
  if (! SPIFFS.begin()) {
    SPIFFS.format();
    Serial.println("Formatting FS");
    if (! SPIFFS.begin()) {
      Serial.println("Failed to format FS");
    } else {
      has_fs = true;
    }
  } else {
    has_fs = true;
  }
  if (has_fs) {
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    Serial.print("FS Used: ");
    Serial.print(fs_info.usedBytes);
    Serial.print(" Free: ");
    Serial.print(fs_info.totalBytes - fs_info.usedBytes);
    Serial.print(" Blocksize: ");
    Serial.println(fs_info.blockSize);
    Dir dir = SPIFFS.openDir("/");
    while(dir.next()) {
      Serial.println("File: " + dir.fileName());
    }
  }
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }
  
  server.on("/", handleRoot);
  server.on("/ir", handleIr);
  server.on("/list", handleList);
 
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started");
}
 
void loop(void){
  server.handleClient();
  if (! cmdQueue.isEmpty()) {
    Cmd *nextCmd = cmdQueue.peek();
    Serial.println("Sendng code: Repeat=" + String(nextCmd->repeat) + " queue size= " + cmdQueue.count());
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
