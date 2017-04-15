#!/usr/bin/python3

import paho.mqtt.client as mqtt
import os
import sys
import argparse
import datetime
import hashlib
import base64
import time

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    # client.subscribe("esp8266-in/#")
    # client.subscribe("esp8266-out/#")
    # client.subscribe("esp8266/#")

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print("%s   %-30s = %s" % (str(datetime.datetime.now()), msg.topic, str(msg.payload)));

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", help="Input file");
    parser.add_argument("--id", help="Firmware ID (n HEX)");
    parser.add_argument("--broker", help="MQTT broker");
    parser.add_argument("--port", help="MQTT broker port");
    args = parser.parse_args()
    if not os.path.isfile(args.bin):
        print("File: " + args.bin + " does not exist")
        sys.exit(1)
    print("File: " + args.bin + " ID: " + args.id)

    if not args.broker:
        args.broker = "127.0.0.1"
    if not args.port:
        args.port = 1883
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    fh = open(args.bin, "rb")
    data = fh.read();
    fh.close()
    md5 = base64.b64encode(hashlib.md5(data).digest())
    payload = "md5:%s,len:%d" %(md5.decode(), len(data))
    print(payload)
    client.connect(args.broker, args.port, 60)
    b64data = base64.b64encode(data);
    client.publish("esp8266-in/ota/" + args.id + "/start", payload)
    time.sleep(10)
    pos = 0
    while len(b64data):
        d = b64data[0: 1024]
        b64data = b64data[1024:]
        client.publish("esp8266-in/ota/" + args.id + "/" + str(pos), d)
        pos += int(len(d) * 3 / 4);
        time.sleep(0.2)
        if pos % (1024 *10) == 0:
            print("Transmitted %d bytes" % (pos))
    print("Completed send")
    client.publish("esp8266-in/ota/" + args.id + "/check", "")
    client.loop_forever()
    client.disconnect()
main()
