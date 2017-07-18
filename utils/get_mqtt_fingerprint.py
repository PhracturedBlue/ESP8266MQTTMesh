#!/usr/bin/env python

# This script originally came from:
# https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py

import argparse
import ssl
import hashlib
import binascii
import sys
import socket

# The following came from https://tools.ietf.org/html/rfc5754#section-3
# This is crude, but works without requireing additional dependencies

signatures = {
    "dsa_sha224":   "30 0b 06 09 60 86 48 01 65 03 04 03 01",
    "dsa_sha256":   "30 0b 06 09 60 86 48 01 65 03 04 03 02",
    "rsa_sha224":   "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0e 05 00",
    "rsa_sha256":   "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0b 05 00",
    "rsa_sha384":   "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0c 05 00",
    "rsa_sha512":   "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0d 05 00",
    "ecdsa_sha224": "30 0a 06 08 2a 86 48 ce 3d 04 03 01",
    "ecdsa_sha256": "30 0a 06 08 2a 86 48 ce 3d 04 03 02",
    "ecdsa_sha384": "30 0a 06 08 2a 86 48 ce 3d 04 03 03",
    "ecdsa_sha512": "30 0a 06 08 2a 86 48 ce 3d 04 03 04"
}

parser = argparse.ArgumentParser(description='Compute SSL/TLS fingerprints.')
parser.add_argument('--host', required=True)
parser.add_argument('--port', default=8883)

args = parser.parse_args()

try:
    cert_pem = ssl.get_server_certificate((args.host, args.port))
    cert_der = ssl.PEM_cert_to_DER_cert(cert_pem)
except socket.error as e:
    if str(e).find("[Errno 111]") is not -1:
        print("ERROR: Could not connect to %s:%s" % (args.host, args.port))
    elif str(e).find("[Errno 104]") is not -1:
        print("ERROR: Mosquitto broker does not appear to be using TLS at %s:%s" % (args.host, args.port))
    print(e)
    sys.exit(1)

matches = []
for k in signatures:
    fingerprint = binascii.a2b_hex(signatures[k].replace(" ", ""))
    if cert_der.find(fingerprint) is not -1:
        matches.append(k)
if not matches:
    print("WARNING: Couldn't identify signature algorithm")
else:
    print("INFO: Found signature algorithm: " + ", ".join(matches))
    for sig in ("rsa_sha384", "rsa_sha512", "ecdsa_sha384", "ecdsa_sha512"):
        if sig in matches:
            print("ERROR: MQTT broker is using a %s signature which will not work with ESP8266" % (sig))

sha1 = hashlib.sha1(cert_der).hexdigest()

print("const uint8_t MQTT_FINGERPRINT[] = {0x" + ",0x".join([sha1[i:i+2] for i in range(0, len(sha1), 2)]) + "};")
