Darner
======

Darner is a very simple message queue built on [boost::asio](http://www.boost.org/libs/asio/) and
[leveldb](http://code.google.com/p/leveldb/).  It supports the memcache protocol.

A single darner server has a set of queues identified by name, which is also the filename of that queue's journal file.
Each queue is a strictly-ordered FIFO of items of binary data.

### Protocol
------------

The official memcache protocol is described here:
[protocol.txt](https://github.com/memcached/memcached/blob/master/doc/protocol.txt)

The darner implementation of the memcache protocol commands is described below.

- `SET <queue-name> <# bytes>`

  Add an item to a queue. It may fail if the queue has a size or item limit and it's full.

- `GET <queue-name>[options]`

  Remove an item from a queue. It will return an empty response immediately if the queue is empty. The queue name may be
  followed by options separated by `/`:

    - `/t=<milliseconds>`

      Wait up to a given time limit for a new item to arrive. If an item arrives on the queue within this timeout, it's
      returned as normal. Otherwise, after that timeout, an empty response is returned.

    - `/open`

      Tentatively remove an item from the queue. The item is returned as usual but is also set aside in case the client
      disappears before sending a "close" request. (See "Reliable Reads" below.)

    - `/close`

      Close any existing open read. (See "Reliable Reads" below.)

    - `/abort`

      Cancel any existing open read, returing that item to the head of the queue. It will be the next item fetched.

  For example, to open a new read, waiting up to 500msec for an item:

        GET work/t=500/open

  Or to close an existing read and open a new one:

        GET work/close/open

- `DELETE <queue-name>`

  Drop a queue, discarding any items in it, and deleting any associated journal files.

- `FLUSH <queue-name>`

  Discard all items remaining in this queue. The queue remains live and new items can be added. The time it takes to
  flush will be linear to the current queue size, and any other activity on this queue will block while it's being
  flushed.

- `FLUSH_ALL`

  Discard all items remaining in all queues. The queues are flushed one at a time, as if kestrel received a `FLUSH`
  command for each queue.

- `VERSION`

  Display the kestrel version in a way compatible with memcache.

- `STATS`

  Display server stats in memcache style. They're described below.
