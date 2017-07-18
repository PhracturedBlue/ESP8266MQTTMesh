#!/usr/bin/env python

# This script originally came from:
# https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py

import argparse
import ssl
import hashlib
import binascii

# The following came from https://tools.ietf.org/html/rfc5754#section-3
# This is crude, but works without requireing additional dependencies

signatures = {
    "dsa_sha224": "30 0b 06 09 60 86 48 01 65 03 04 03 01",
    "dsa_sha256": "30 0b 06 09 60 86 48 01 65 03 04 03 02",
    "rsa_sha224": "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0e 05 00",
    "rsa_sha256": "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0b 05 00",
    "rsa_sha384": "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0c 05 00",
    "rsa_sha512": "30 0d 06 09 2a 86 48 86 f7 0d 01 01 0d 05 00"
}

parser = argparse.ArgumentParser(description='Compute SSL/TLS fingerprints.')
parser.add_argument('--host', required=True)
parser.add_argument('--port', default=8883)

args = parser.parse_args()

cert_pem = ssl.get_server_certificate((args.host, args.port))
cert_der = ssl.PEM_cert_to_DER_cert(cert_pem)

matches = []
for k in signatures:
    fingerprint = binascii.a2b_hex(signatures[k].replace(" ", ""))
    if -1 != cert_der.find(fingerprint):
        matches.append(k)
if not matches:
    print("WARNING: Couldn't identify signature algorithm")
else:
    print("INFO: Found signature algorithm: " + ", ".join(matches))
    if "rsa_sha512" in matches:
        print("ERROR: MQTT broker is using a sha512 signature which will not work with ESP8266")
    if "rsa_sha384" in matches:
        print("ERROR: MQTT broker is using a sha384 signature which will not work with ESP8266")

sha1 = hashlib.sha1(cert_der).hexdigest()

print("const uint8_t MQTT_FINGERPRINT[] = {0x" + ",0x".join([sha1[i:i+2] for i in range(0, len(sha1), 2)]) + "};")
