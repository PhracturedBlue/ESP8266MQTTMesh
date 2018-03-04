#!/usr/bin/python3

import paho.mqtt.client as mqtt
import os
import sys
import argparse
import datetime
import hashlib
import base64
import time
import ssl
import re
import queue


topic = "esp8266-"
inTopic = topic + "in"
outTopic = topic + "out"
send_topic = ""
name=""
passw=""
q = queue.Queue();

def regex(pattern, txt, group):
    group.clear()
    match = re.search(pattern, txt)
    if match:
        if match.groupdict():
            for k,v in match.groupdict().items():
                group[k] = v
        else:
            group.extend(match.groups())
        return True
    return False

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("{}/#".format(outTopic))

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    #esp8266-out/mesh_esp8266-6/check=MD5 Passed
    match = []
    if regex(r'/([0-9a-f]+)/ota/erase$', msg.topic, match):
        q.put(["erase", match[0]])
    elif regex(r'([0-9a-f]+)/ota/md5/([0-9a-f]+)', msg.topic, match):
        q.put(["md5", match[0], match[1], msg.payload])
    elif regex(r'([0-9a-f]+)/ota/check$', msg.topic, match):
        q.put(["check", match[0], msg.payload])
    else:
        #print("%s   %-30s = %s" % (str(datetime.datetime.now()), msg.topic, str(msg.payload)));
        pass
        

def wait_for(nodes, msgtype, maxTime, retries=0, pubcmd=None):
    seen = {}
    origTime = time.time()
    startTime = origTime
    while True:
        try:
            msg = q.get(True, 0.1)
            if msg[0] == msgtype and (not nodes or msg[1] in nodes):
                node = msg[1]
                seen[node] = msg
            else:
                print("Got unexpected {} for node {}".format(msgtype, msg[1], msg))
        except queue.Empty:
            if time.time() - startTime < maxTime:
                continue
            if retries:
                retries -= 1
                print("{} node(s) missed the message, retrying".format(len(nodes) - len(seen.keys())))
                client.publish(pubcmd[0], pubcmd[1])
                startTime = time.time()
            else:
                print("{} node(s) missed the message and no retires left".format(len(nodes) - len(seen.keys())))
        if nodes and len(seen.keys()) == len(nodes):
            break
    #print("Elapsed time waiting for {} messages: {} seconds".format(msgtype, time.time() - origTime))
    return seen

def send_firmware(client, data, nodes):
    md5 = base64.b64encode(hashlib.md5(data).digest())
    payload = "md5:%s,len:%d" %(md5.decode(), len(data))
    print("Erasing...")
    client.publish("{}start".format(send_topic), payload)
    nodes = list(wait_for(nodes, 'erase', 10).keys())
    print("Updating firmware on the following nodes:\n\t{}".format("\n\t".join(nodes)))
    pos = 0
    while len(data):
        d = data[0:768]
        b64d = base64.b64encode(d)
        data = data[768:]
        client.publish("{}{}".format(send_topic, str(pos)), b64d)
        expected_md5 = hashlib.md5(d).hexdigest().encode('utf-8')
        seen = {}
        retries = 1
        seen = wait_for(nodes, 'md5', 1.0, 1, ["{}{}".format(send_topic, str(pos)), b64d])
        for node in nodes:
            if node not in seen:
                print("No MD5 found for {} at 0x{}".format(node, pos))
                return
            addr = int(seen[node][2], 16)
            md5 = seen[node][3]
            if pos != addr:
                print("Got unexpected address 0x{} (expected: 0x{}) from node {}".format(addr, pos, node))
                return
            if md5 != expected_md5:
                print("Got unexpected md5 for node {} at 0x{}".format(node, addr))
                print("\t {} (expected: {})".format(md5, expected_md5))
                return
        pos += len(d)
        if pos % (768 * 13) == 0:
            print("Transmitted %d bytes" % (pos))
    print("Completed send")
    client.publish("{}check".format(send_topic), "")
    seen = wait_for(nodes, 'check', 5)
    err = False
    for node in nodes:
        if node not in seen:
            print("No verify result found for {}".format(node))
            err = True
        if seen[node][2] != b'MD5 Passed':
            print("Node {} did not pass final MD5 check: {}".format(node, seen[node][2]))
            err = True
    if err:
        return
    print("Checksum verified. Flashing and rebooting now...")
    client.publish("{}flash".format(send_topic), "")

def main():
    global inTopic, outTopic, name, passw, send_topic
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", help="Input file");
    parser.add_argument("--id", help="Firmware ID (n HEX)");
    parser.add_argument("--broker", help="MQTT broker");
    parser.add_argument("--port", help="MQTT broker port");
    parser.add_argument("--user", help="MQTT broker user");
    parser.add_argument("--password", help="MQTT broker password");
    parser.add_argument("--ssl", help="MQTT broker SSL support");
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

    if args.user:
       name = args.user
    if args.password:
       passw = args.password

    client = mqtt.Client()
    if args.ssl:
       client.tls_set(ca_certs=None, certfile=None, keyfile=None, cert_reqs=ssl.CERT_REQUIRED,tls_version=ssl.PROTOCOL_TLS, ciphers=None)
    if (args.user) or (args.password):
        client.username_pw_set(name,passw)
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(args.broker, args.port, 60)
    client.loop_start()

    fh = open(args.bin, "rb")
    data = fh.read();
    fh.close()

    send_firmware(client, data, [args.node] if args.node else [])

    client.loop_stop()
    client.disconnect()
main()
