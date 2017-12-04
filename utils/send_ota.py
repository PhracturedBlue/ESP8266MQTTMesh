#!/usr/bin/python3

import paho.mqtt.client as mqtt
import os
import sys
import argparse
import datetime
import hashlib
import base64
import time

topic = "esp8266-"
inTopic = topic + "in"
outTopic = topic + "out"

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("{}/#".format(outTopic))

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    #esp8266-out/mesh_esp8266-6/check=MD5 Passed
    print("%s   %-30s = %s" % (str(datetime.datetime.now()), msg.topic, str(msg.payload)));

def main():
    global inTopic, outTopic
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", help="Input file");
    parser.add_argument("--id", help="Firmware ID (n HEX)");
    parser.add_argument("--broker", help="MQTT broker");
    parser.add_argument("--port", help="MQTT broker port");
    parser.add_argument("--topic", help="MQTT mesh topic base (default: {}".format(topic))
    parser.add_argument("--intopic", help="MQTT mesh in-topic (default: {}".format(inTopic))
    parser.add_argument("--outtopic", help="MQTT mesh out-topic (default: {}".format(outTopic))
    parser.add_argument("--node", help=("Specific node to send firmware to"))
    args = parser.parse_args()
    if not os.path.isfile(args.bin):
        print("File: " + args.bin + " does not exist")
        sys.exit(1)

    if args.topic:
        inTopic = args.topic + "in"
        outTopic = args.topic + "out"
    if args.intopic:
        inTopic = args.intopic
    if args.outtopic:
        outTopic = args.outtopic

    if args.id:
        send_topic = "{}/ota/{}/".format(inTopic, args.id)
    elif args.node:
        send_topic = "{}/ota/{}/".format(inTopic, args.node)
    else:
        print("Must specify either --id or --node")
        sys.exit(1)
    print("File: {}".format(args.bin))
    print("Sending to topic: {}".format(send_topic))
    print("Listening to topic: {}".format(outTopic))

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
    print("Erasing...")
    client.publish("{}start".format(send_topic), payload)
    time.sleep(10)
    pos = 0
    while len(b64data):
        d = b64data[0: 1024]
        b64data = b64data[1024:]
        client.publish("{}{}".format(send_topic, str(pos)), d)
        pos += int(len(d) * 3 / 4);
        time.sleep(0.2)
        if pos % (768 * 13) == 0:
            print("Transmitted %d bytes" % (pos))
    print("Completed send")
    client.publish("{}check".format(send_topic), "")
    time.sleep(5);
    client.publish("{}flash".format(send_topic), "")
    client.loop_forever()
    client.disconnect()
main()
