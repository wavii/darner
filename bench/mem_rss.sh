#!/bin/bash

# memory test - how much resident memory is used after growing and then shrinking a queue
# kestrel is on port 22133
# darner is on port 22134
# before running this test, be sure to delete the db_bench queue and restart both services

for i in 0 1024 2048 4096 8192 16384 32768 65536 131072 262024 524048
do
   printf "kestrel  %7i requests: " "$i"
   ./db -p 22133 -s $i -g 0 -i 1024 >/dev/null
   ./db -p 22133 -s 0 -g $i -i 1024 >/dev/null
   pgrep -f "/opt/kestrel/kestrel" | xargs -I'{}' sudo cat /proc/{}/status | grep -i vmrss | awk '{print $2, $3}'
done

for i in 0 1024 2048 4096 8192 16384 32768 65536 131072 262024 524048
do
   printf "darner   %7i requests: " "$i"
   ./db -p 22134 -s $i -g 0 -i 1024 >/dev/null
   ./db -p 22134 -s 0 -g $i -i 1024 >/dev/null
   pgrep darner | xargs -I'{}' sudo cat /proc/{}/status | grep -i vmrss | awk '{print $2, $3}'
done
