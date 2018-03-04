#!/bin/bash
#This script is a slightly modified copy of:
#https://github.com/me-no-dev/ESPAsyncTCP/blob/master/ssl/gen_server_cert.sh
cat > ca_cert.conf << EOF  
[ req ]
distinguished_name     = req_distinguished_name
prompt                 = no

[ req_distinguished_name ]
 O                      = Espressif Systems
EOF

openssl genrsa -out axTLS.ca_key.pem 2048
openssl req -new -config ./ca_cert.conf -key axTLS.ca_key.pem -out axTLS.ca_x509.req
openssl x509 -req -sha1 -days 5000 -signkey axTLS.ca_key.pem -CAkey axTLS.ca_key.pem -in axTLS.ca_x509.req -out axTLS.ca_x509.pem

cat > certs.conf << EOF  
[ req ]
distinguished_name     = req_distinguished_name
prompt                 = no

[ req_distinguished_name ]
 O                      = axTLS on ESP8266
 CN                     = esp8266.local
EOF

openssl genrsa -out axTLS.key_1024.pem 1024
openssl req -new -config ./certs.conf -key axTLS.key_1024.pem -out axTLS.x509_1024.req
openssl x509 -req -sha1 -CAcreateserial -days 5000 -CA axTLS.ca_x509.pem -CAkey axTLS.ca_key.pem -in axTLS.x509_1024.req -out axTLS.x509_1024.pem

openssl rsa -outform DER -in axTLS.key_1024.pem -out axTLS.key_1024
openssl x509 -outform DER -in axTLS.x509_1024.pem -out axTLS.x509_1024.cer

cat axTLS.key_1024 > server.key
cat axTLS.x509_1024.cer > server.cer
python -c 'import os; import hashlib; os.write(1,hashlib.sha1(open("server.cer", "rb").read()).digest())' > fingerprint

echo "const uint8_t ssl_key[] =" > ssl_cert.h
hexdump -v -e '16/1 "_x%02X" "\n"' server.key | sed 's/_/\\/g; s/\\x  //g; s/.*/    "&"/' >> ssl_cert.h
echo ";" >> ssl_cert.h
echo "const uint32_t ssl_key_len = `cat server.key | wc -c`;" >> ssl_cert.h

echo "const uint8_t ssl_cert[] =" >> ssl_cert.h
hexdump -v -e '16/1 "_x%02X" "\n"' server.cer | sed 's/_/\\/g; s/\\x  //g; s/.*/    "&"/' >> ssl_cert.h
echo ";" >> ssl_cert.h
echo "const uint32_t ssl_cert_len = `cat server.cer | wc -c`;" >> ssl_cert.h

echo "const uint8_t ssl_fingerprint[] =" >> ssl_cert.h
hexdump -v -e '16/1 "_x%02X" "\n"' fingerprint | sed 's/_/\\/g; s/\\x  //g; s/.*/    "&"/' >> ssl_cert.h
echo ";" >> ssl_cert.h

rm axTLS.* ca_cert.conf certs.conf
