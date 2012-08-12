#!/bin/bash

# packing test - check get/set throughput after having grown the queue to different sizes
# kestrel is on port 22133
# darner is on port 22134

# kestrel reaches its best performance after a warmup period

echo -n "warming up kestrel..."

./db -p 22133 -s 100000 -g 100000 >/dev/null

echo "done."

echo -ne "flush db_bench\r\n" | nc localhost 22133 >/dev/null

for i in 0 4096 16384 65536 262144 1048576 4194304
do
   ./db -p 22133 -s $i -g 0 -i 1024 >/dev/null
   echo -n "kestrel $i sets: "
   ./db -p 22133 -s 100000 -g 100000 -i 1024 | grep -i "requests per second" | awk -F"    " '{print $2}'
done

for i in 0 4096 16384 65536 262144 1048576 4194304
do
   ./db -p 22134 -s $i -g 0 -i 1024 >/dev/null
   echo -n "darner $i sets: "
   ./db -p 22134 -s 100000 -g 100000 -i 1024 | grep -i "requests per second" | awk -F"    " '{print $2}'
done

