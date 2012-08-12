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
kestrel 1 conns: 6987.88 #/sec (mean)
kestrel 2 conns: 8910.67 #/sec (mean)
kestrel 5 conns: 9739.47 #/sec (mean)
kestrel 10 conns: 10541.9 #/sec (mean)
kestrel 50 conns: 11696.6 #/sec (mean)
kestrel 100 conns: 11851.2 #/sec (mean)
kestrel 200 conns: 8147.63 #/sec (mean)
kestrel 300 conns: 4916.06 #/sec (mean)
darner 1 conns: 10458.1 #/sec (mean)
darner 2 conns: 18104.5 #/sec (mean)
darner 5 conns: 20913.9 #/sec (mean)
darner 10 conns: 23411 #/sec (mean)
darner 50 conns: 24067.4 #/sec (mean)
darner 100 conns: 23880.6 #/sec (mean)
darner 200 conns: 23504.5 #/sec (mean)
darner 300 conns: 23866.3 #/sec (mean)
darner 400 conns: 22568.3 #/sec (mean)
darner 600 conns: 20777.1 #/sec (mean)
darner 800 conns: 21121.6 #/sec (mean)
darner 1000 conns: 19807.9 #/sec (mean)
memcache 1 conns: 11610.4 #/sec (mean)
memcache 2 conns: 20768.4 #/sec (mean)
memcache 5 conns: 25510.2 #/sec (mean)
memcache 10 conns: 35298.3 #/sec (mean)
memcache 50 conns: 40249.5 #/sec (mean)
memcache 100 conns: 41571.4 #/sec (mean)
memcache 200 conns: 41677.8 #/sec (mean)
memcache 300 conns: 43346.3 #/sec (mean)
memcache 400 conns: 41169.2 #/sec (mean)
memcache 600 conns: 39880.4 #/sec (mean)
memcache 800 conns: 36264.7 #/sec (mean)
memcache 1000 conns: 9547.91 #/sec (mean)
```

# Queue Packing

This tests the queue server's behavior with a backlog of items.  The challenge for the queue server is to serve items
that no longer all fit in memory.  Absolute throughput isn't important here - item sizes are large to quickly saturate
free memory.  Instead it's important for the throughput to flatten out as the backlog grows.

![Queue Packing Benchmark](/wavii/darner/raw/master/docs/images/bench_queue_packing.png)

```
ubuntu@domU-12-31-39-0E-0C-72:~/darner$ bench/packing.sh
warming up kestrel...done.
kestrel 0 sets: 9700.73 #/sec (mean)
kestrel 4096 sets: 10039.7 #/sec (mean)
kestrel 16384 sets: 9595.09 #/sec (mean)
kestrel 65536 sets: 9581.76 #/sec (mean)
kestrel 262144 sets: 8785.42 #/sec (mean)
kestrel 1048576 sets: 8916.23 #/sec (mean)
kestrel 4194304 sets: 8460.59 #/sec (mean)
darner 0 sets: 17124.8 #/sec (mean)
darner 4096 sets: 14646.6 #/sec (mean)
darner 16384 sets: 12122.7 #/sec (mean)
darner 65536 sets: 10717 #/sec (mean)
darner 262144 sets: 10970.3 #/sec (mean)
darner 1048576 sets: 11596.9 #/sec (mean)
darner 4194304 sets: 11017.5 #/sec (mean)
```
