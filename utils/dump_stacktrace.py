#!/usr/bin/python
import subprocess
import sys
import re
import os

firmware = sys.argv[1]
stacktrace = sys.argv[2]
objdump = os.environ['HOME'] + "/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-objdump"

dump = subprocess.check_output([objdump, '-S',firmware]).split('\n')
funcs = []
start = None
end = None
for line in dump:
    #40208544 <_ZN15ESP8266MQTTMesh8setup_APEv>:
    match = re.match(r'([0-9a-f]{8})\s+<(.*)>:', line)
    if match:
        funcs.append([int(match.group(1), 16), match.group(2)])
    match = re.match(r'([0-9a-f]{8}):',line)
    if match:
        add = int(match.group(1), 16)
        if not end or add > end:
            end = add
        if not start or add < start:
            start = add
with open(stacktrace, "r") as fh:
    in_stack = False
    for line in fh:
        if re.search(r'>>>stack>>>', line):
            in_stack = True
        if in_stack:
            addrs = re.split(r'[: ]+', line)
            for addr in addrs:
               try:
                   add = int(addr, 16)
               except:
                   continue
               if add < start or add > end:
                   #print("Ignoring: %s (%08x, %08x)" %  (addr, start, end))
                   continue
               for i in range(0, len(funcs)):
                   if funcs[i][0] <= add:
                       continue
                   print("%s : %s" % (addr, funcs[i-1][1]))
                   break



