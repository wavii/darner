These benchmarks were run on an [Amazon EC2 m1.large](http://aws.amazon.com/ec2/instance-types/): 7.5GB memory, 2 cores,
64-bit ubuntu. The kestrel server was run with OpenJDK 1.6, with the default JVM options provided from its example init
scripts.

# Queue Flooding

How quickly can we flood items through an empty queue?  This tests the raw throughput of the server.  We also include
memcache as an upper bound - a throughput at which we are likely saturating on `send/recv` syscalls.  We had to stop the
kestrel benchmark at 300 concurrent connections - anything higher caused connection errors and timeouts.

![Queue Flood Benchmark](/wavii/darner/raw/master/docs/images/bench_queue_flood.png)

```
ubuntu@domU-12-31-39-0E-0C-72:~/darner$ bench/flood.sh 
warming up kestrel...done.
kestrel     1 conns: 6919.94 #/sec (mean)
kestrel     2 conns: 9042 #/sec (mean)
kestrel     5 conns: 9775.17 #/sec (mean)
kestrel    10 conns: 10526.9 #/sec (mean)
kestrel    50 conns: 11318 #/sec (mean)
kestrel   100 conns: 11693.2 #/sec (mean)
kestrel   200 conns: 5696.71 #/sec (mean)
kestrel   300 conns: 3260.36 #/sec (mean)
darner      1 conns: 10032.1 #/sec (mean)
darner      2 conns: 16572.8 #/sec (mean)
darner      5 conns: 21006.2 #/sec (mean)
darner     10 conns: 22182.8 #/sec (mean)
darner     50 conns: 24697.5 #/sec (mean)
darner    100 conns: 23960.7 #/sec (mean)
darner    200 conns: 24160.4 #/sec (mean)
darner    300 conns: 23781.2 #/sec (mean)
darner    400 conns: 21755.7 #/sec (mean)
darner    600 conns: 22019.2 #/sec (mean)
darner    800 conns: 20076.3 #/sec (mean)
darner   1000 conns: 19648.3 #/sec (mean)
memcache    1 conns: 11516.1 #/sec (mean)
memcache    2 conns: 21879.4 #/sec (mean)
memcache    5 conns: 27700.8 #/sec (mean)
memcache   10 conns: 37126.4 #/sec (mean)
memcache   50 conns: 43412.2 #/sec (mean)
memcache  100 conns: 41126.9 #/sec (mean)
memcache  200 conns: 38610 #/sec (mean)
memcache  300 conns: 41347.9 #/sec (mean)
memcache  400 conns: 40833 #/sec (mean)
memcache  600 conns: 38299.5 #/sec (mean)
memcache  800 conns: 37167.8 #/sec (mean)
memcache 1000 conns: 34506.6 #/sec (mean)
```

# Fairness

How does the queue server deal with messages of varying sizes?  This benchmark pushes large messages through the queue
in order to see whether they cause latency spikes for smaller requests, which can lead to timeouts.  For the graph
below, a flatter curve means each request is more fairly served in time.

![Fairness Benchmark](/wavii/darner/raw/master/docs/images/bench_fairness.png)

```
ubuntu@domU-12-31-39-0E-0C-72:~/darner$ bench/fairness.sh 
warming up kestrel...done.
kestrel stats:
Concurrency Level:      10
Gets:                   0
Sets:                   100000
Time taken for tests:   215.285 seconds
Bytes read:             800000 bytes
Read rate:              3.62891 Kbytes/sec
Bytes written:          8700000 bytes
Write rate:             39.4644 Kbytes/sec
Requests per second:    464.501 #/sec (mean)
Time per request:       21503.9 us (mean)

Percentage of the requests served within a certain time (us)
  50%:    969
  66%:    1596
  75%:    2777
  80%:    5644
  90%:    34467
  95%:    90138
  98%:    299754
  99%:    473703
 100%:    1841528 (longest request)
darner stats:
Concurrency Level:      10
Gets:                   0
Sets:                   100000
Time taken for tests:   20.622 seconds
Bytes read:             800000 bytes
Read rate:              37.8843 Kbytes/sec
Bytes written:          8700000 bytes
Write rate:             411.992 Kbytes/sec
Requests per second:    4849.19 #/sec (mean)
Time per request:       2060.75 us (mean)

Percentage of the requests served within a certain time (us)
  50%:    869
  66%:    911
  75%:    944
  80%:    968
  90%:    1117
  95%:    2523
  98%:    43498
  99%:    43951
 100%:    91903 (longest request)
 ```

# Queue Packing

This tests the queue server's behavior with a backlog of items.  The challenge for the queue server is to serve items
that no longer all fit in memory.  Absolute throughput isn't important here - item sizes are large to quickly saturate
free memory.  Instead it's important for the throughput to flatten out as the backlog grows.

![Queue Packing Benchmark](/wavii/darner/raw/master/docs/images/bench_queue_packing.png)

```
ubuntu@domU-12-31-39-0E-0C-72:~/darner$ bench/packing.sh 
warming up kestrel...done.
kestrel       0 sets: 9777.08 #/sec (mean)
kestrel    4096 sets: 9490.37 #/sec (mean)
kestrel   16384 sets: 9777.56 #/sec (mean)
kestrel   65536 sets: 9478.22 #/sec (mean)
kestrel  262144 sets: 8689.99 #/sec (mean)
kestrel 1048576 sets: 8735.15 #/sec (mean)
kestrel 4194304 sets: 8467.04 #/sec (mean)
darner        0 sets: 16380 #/sec (mean)
darner     4096 sets: 14951 #/sec (mean)
darner    16384 sets: 12043.1 #/sec (mean)
darner    65536 sets: 10691.8 #/sec (mean)
darner   262144 sets: 10810.8 #/sec (mean)
darner  1048576 sets: 11152 #/sec (mean)
darner  4194304 sets: 10980.6 #/sec (mean)
```
