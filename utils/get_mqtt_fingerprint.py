#!/usr/bin/env python

# This script originally came from:
# https://github.com/marvinroger/async-mqtt-client/blob/master/scripts/get-fingerprint/get-fingerprint.py

import argparse
import ssl
import hashlib

parser = argparse.ArgumentParser(description='Compute SSL/TLS fingerprints.')
parser.add_argument('--host', required=True)
parser.add_argument('--port', default=8883)

args = parser.parse_args()
print(args.host)

cert_pem = ssl.get_server_certificate((args.host, args.port))
cert_der = ssl.PEM_cert_to_DER_cert(cert_pem)

md5 = hashlib.md5(cert_der).hexdigest()
sha1 = hashlib.sha1(cert_der).hexdigest()

print("const uint8_t MQTT_FINGERPRINT[] = {0x" + ",0x".join([sha1[i:i+2] for i in range(0, len(sha1), 2)]) + "};")
