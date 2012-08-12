#!/bin/bash

# flood test - try flood get/set at different concurrencies to measure throughput
# kestrel is on port 22133
# darner is on port 22134

# kestrel reaches its best performance after a warmup period

echo -n "warming up kestrel..."

./db -p 22133 -s 100000 -g 100000 >/dev/null

echo "done."

# fill the queue with just a few items, to ensure there's always an item available
echo -ne "flush db_bench\r\n" | nc localhost 22134 >/dev/null
./db -p 22133 -s 512 -g 0 >/dev/null

for i in 1 2 5 10 50 100 200 300
do
   sync # just in case dirty pages are lying around, don't leak across each run
   printf "kestrel  %4i conns: " "$i"
   ./db -p 22133 -s 100000 -g 100000 -c $i | grep -i "requests per second" | awk -F"    " '{print $2}'
done

echo -ne "flush db_bench\r\n" | nc localhost 22134 >/dev/null
./db -p 22134 -s 512 -g 0 >/dev/null

for i in 1 2 5 10 50 100 200 300 400 600 800 1000
do
   sync
   printf "darner   %4i conns: " "$i"
   ./db -p 22134 -s 100000 -g 100000 -c $i | grep -i "requests per second" | awk -F"    " '{print $2}'
done

for i in 1 2 5 10 50 100 200 300 400 600 800 1000
do
   sync
   printf "memcache %4i conns: " "$i"
   ./db -p 11211 -s 100000 -g 100000 -c $i | grep -i "requests per second" | awk -F"    " '{print $2}'
done
