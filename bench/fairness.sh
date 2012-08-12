#!/bin/bash

# fairness test - check how much set throughput suffers when small and large messages mix
# kestrel is on port 22133
# darner is on port 22134

# kestrel reaches its best performance after a warmup period

echo -n "warming up kestrel..."

./db -p 22133 -s 100000 -g 100000 >/dev/null

echo "done."

echo -ne "flush db_bench\r\n" | nc localhost 22133 >/dev/null

./db -p 22133 -s 100000 -g 0 -i 8388608 >/dev/null &
echo "kestrel stats:"
./db -p 22133 -s 100000 -g 0

kill %1 # kill off backgrounded flood job

./db -p 22134 -s 100000 -g 0 -i 8388608 >/dev/null &
echo "darner stats:"
./db -p 22134 -s 100000 -g 0

kill %2
