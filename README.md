# Darner

Darner is a very simple message queue server.  Unlike in-memory servers such as [redis](http://redis.io/), Darner is
designed to handle queues much larger than what can be held in RAM.  And unlike enterprise queue servers such as
[RabbitMQ](http://www.rabbitmq.com/), Darner keeps all messages **out of process**, relying instead on the kernel's
virtual memory manager via [log-structured storage](https://code.google.com/p/leveldb/).

The result is a durable queue server that uses a small amount of in-resident memory regardless of queue size, while
still achieving [remarkable performance](/wavii/darner/blob/master/docs/benchmarks.md).

Darner is based on Robey Pointer's [Kestrel](/robey/kestrel) simple, distributed message queue.  Like Kestrel, Darner
follows the "No talking! Shhh!" approach to distributed queues:  A single Darner server has a set of queues identified
by name.  Each queue is a strictly-ordered FIFO, and querying from a fleet of Darner servers provides a loosely-ordered
queue.  Darner also supports Kestrel's two-phase reliable fetch: if a client disconnects before confirming it handled
a message, the message will be handed to the next client.

Compared to Kestrel, Darner boasts much higher throughput, better concurrency, an order of magnitude better tp99, and
uses an order of magnitude less memory.  But Darner has less configuration, and far fewer features than Kestrel. Check
out the [benchmarks](/wavii/darner/blob/master/docs/benchmarks.md)!

Darner is used at [Wavii](http://wavii.com/), and is written and maintained by [Erik Frey](/erikfrey).

## Installing

You'll need build tools, [CMake](http://www.cmake.org/), [Boost](http://www.boost.org/), and
[LevelDB](https://code.google.com/p/leveldb/)/[snappy](https://code.google.com/p/snappy/) to build Darner:

```bash
sudo apt-get install -y build-essential cmake libboost-all-dev libsnappy-dev
wget https://leveldb.googlecode.com/files/leveldb-1.5.0.tar.gz
tar xvzf leveldb-1.5.0.tar.gz && cd leveldb
make
sudo mv libleveldb.* /usr/local/lib/ && sudo chown root:root /usr/local/lib/libleveldb.*
cd ..
```

Then you can fetch and install Darner:

```bash
git clone git@github.com:wavii/darner.git
cd darner
cmake . && make && sudo make install
```

## Running

Make a directory for Darner to store its queues, say `/var/spool/darner/`, then run Darner like so.

```bash
vagrant@ubuntu-oneiric:~/workspace/darner$ ./darner -d /var/spool/darner/
[INFO] 2012-Aug-13 03:59:41.047739: darner: queue server
[INFO] 2012-Aug-13 03:59:41.048051: build: Aug 12 2012 (22:24:28) v0.0.1 (c) Wavii, Inc.
[INFO] 2012-Aug-13 03:59:41.048132: listening on port: 22133
[INFO] 2012-Aug-13 03:59:41.048507: data dir: /var/spool/darner/
[INFO] 2012-Aug-13 03:59:41.048798: starting up
```

Voila!  By default, Darner listens on port 22133.

## Protocol

Darner follows the same protocol as [Kestrel](/robey/kestrel/blob/master/docs/guide.md#memcache), which is the memcache
protocol.

Currently missing from the Darner implementation but TODO: `/peek`, `FLUSH`, `FLUSH_ALL`, `DELETE`, and some stats.
