#ifndef __DARNER_QUEUE_H__
#define __DARNER_QUEUE_H__

#include <set>
#include <string>
#include <sstream>

#include <boost/array.hpp>
#include <boost/ptr_container/ptr_list.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <leveldb/db.h>
#include <leveldb/comparator.h>

namespace darner {

/*
 * queue is a fifo queue that is O(log(queue size / cache size)) for pushing/popping.  it boasts these features:
 *
 * - an evented wait semantic for queue poppers
 * - popping is two-phase with a begin and an end. ending a pop can either erase it or return it back to the queue.
 * - large items are streamed in a chunk at a time
 *
 * all queue methods are synchronous except for wait(), which starts an async timer on the provided io_service.
 *
 * queue is not thread-safe, it assumes a single-thread calling and operating the provided io_service
 */
class queue
{
public:

   // a queue can only ever have a backlog of 2^64 items.  so at darner's current peak throughput you can only run the
   // server for 23 million years     :(
   typedef boost::uint64_t id_type;
   typedef boost::uint64_t size_type;
   typedef boost::function<void (const boost::system::error_code& error)> wait_callback;

   // open or create the queue at the path
   queue(boost::asio::io_service& ios, const std::string& path);

   // wait up to wait_ms milliseconds for an item to become available, then call cb with success or timeout
   void wait(size_type wait_ms, const wait_callback& cb);

   // returns the number of items in the queue
   size_type count() const;

   // writes out stats (stuff like queue count) to a stream
   void write_stats(const std::string& name, std::ostringstream& out) const;

   // flush the db by deleting it and creating a new one
   void flush_db();

protected:

   friend class iqstream;
   friend class oqstream;

   // queue item points to chunk item via a small metadata header
   class header_type
   {
   public:

      header_type() : beg(0), end(1), size(0) {}
      header_type(id_type _beg, id_type _end, size_type _size)
      : beg(_beg), end(_end), size(_size) {}
      header_type(const std::string& buf)
      {
         *this = *reinterpret_cast<const header_type*>(buf.c_str());
      }

      id_type beg;
      id_type end;
      size_type size;

      void str(std::string& out) const;
   };

   // queue methods aren't meant to be used directly.  instead create an iqstream or oqstream to use it

   /*
    * pushes an item to to the queue.
    */
   void push(id_type& result, const std::string& item);

   /*
    * pushes a header to to the queue.  a header points to a range of chunks in a multi-chunk item.
    */
   void push(id_type& result, const header_type& header);

   /*
    * begins popping an item.  if no items are available, immediately returns false.  once an item pop is begun,
    * it is owned solely by the caller, and must eventually be pop_ended.  pop_begin is constant time.
    */
   bool pop_begin(id_type& result);

   /*
    * once has a pop has begun, call pop_read.  if the item is just one chunk (end - beg < 2), result_item will be
    * immediately populated, otherwise fetch the chunks in the header's range [beg, end).
    */
   void pop_read(std::string& result_item, header_type& result_header, id_type id);

   /*
    * finishes the popping of an item.  if erase = true, deletes the dang ol' item, otherwise returns it
    * back to its position near the tail of the queue.  closing an item with erase = true is constant time, but
    * closing an item with erase = false could take logn time and linear memory for # returned items.
    *
    * the simplest way to address this is to limit the number of items that can be opened at once.
    */
   void pop_end(bool erase, id_type id, const header_type& header);

   // chunk methods:

   /*
    * returns to a header a range of reserved chunks
    */
   void reserve_chunks(header_type& result, size_type count);

   /*
    * writes a chunk
    */
   void write_chunk(const std::string& chunk, id_type chunk_key);

   /*
    * reads a chunk
    */
   void read_chunk(std::string& result, id_type chunk_key);

   /*
    * removes all chunks referred to by a header.  use this when aborting a multi-chunk push.
    */
   void erase_chunks(const header_type& header);

private:

   class key_type
   {
   public:

      enum  { KT_QUEUE = 1, KT_CHUNK = 2 }; // a key can be either a queue or a chunk type

      key_type() : type(KT_QUEUE), id(0) {}

      key_type(char _type, id_type _id) : type(_type), id(_id) {}

      key_type(const leveldb::Slice& s)
      : type(s.data()[sizeof(id_type)]), id(*reinterpret_cast<const id_type*>(s.data())) {}

      leveldb::Slice slice() const;

      int compare(const key_type& other) const
      {
         if (type < other.type)
            return -1;
         else if (type > other.type)
            return 1;
         else if (id < other.id)
            return -1;
         else if (id > other.id)
            return 1;
         return 0;
      }

      unsigned char type;
      id_type id;

   private:

      mutable boost::array<char, sizeof(id_type) + 1> buf_;
   };

   // ties a set of results to a deadline timer
   struct waiter
   {
      waiter(boost::asio::io_service& ios, size_type wait_ms, const wait_callback& _cb)
      : cb(_cb),
        timer(ios, boost::posix_time::milliseconds(wait_ms))
      {
      }

      wait_callback cb;
      boost::asio::deadline_timer timer;
   };

   // compare keys as native uint64's instead of lexically
   class comparator : public leveldb::Comparator
   {
   public:
      int Compare(const leveldb::Slice& a, const leveldb::Slice& b) const
      {
         return key_type(a).compare(key_type(b));
      }
      const char* Name() const { return "queue::comparator"; }
      void FindShortestSeparator(std::string*, const leveldb::Slice&) const {}
      void FindShortSuccessor(std::string*) const {}
   };

   // initalize level db
   void init_db();

   // any operation that adds to the queue should crank a wakeup
   void wake_up();

   // fires either if timer times out or is canceled
   void waiter_wakeup(const boost::system::error_code& e, boost::ptr_list<waiter>::iterator waiter_it);

   // compact the underlying journal, discarding deleted items
   void compact();

   // some leveldb sugar:

   void put(const key_type& key, const std::string& value)
   {
      if (!journal_->Put(leveldb::WriteOptions(), key.slice(), value).ok())
         throw boost::system::system_error(boost::system::errc::io_error, boost::asio::error::get_system_category());
   }

   void get(const key_type& key, std::string& result)
   {
      if (!journal_->Get(leveldb::ReadOptions(), key.slice(), &result).ok())
         throw boost::system::system_error(boost::system::errc::io_error, boost::asio::error::get_system_category());
   }

   void write(leveldb::WriteBatch& batch)
   {
      if (!journal_->Write(leveldb::WriteOptions(), &batch).ok())
         throw boost::system::system_error(boost::system::errc::io_error, boost::asio::error::get_system_category());
   }

   boost::scoped_ptr<comparator> cmp_;
   boost::scoped_ptr<leveldb::DB> journal_;

   // journal has queue keys and chunk keys
   // layout of queue keys in journal is:
   // --- < opened/returned > --- | TAIL | --- < enqueued > --- | HEAD |
   // enqueued items are pushed to head and popped from tail
   // opened are held by a handler (via the key) and not finished yet
   // returned items were released by a connection but not deleted, and behave like enqueued items
   // layout of chunk store in journal is:
   // --- < stored > --- | HEAD |

   key_type queue_head_;
   key_type queue_tail_;
   key_type chunks_head_;

   size_type items_open_; // an open item is < TAIL but not in returned_
   size_type bytes_evicted_; // after we've evicted 32MB from the journal, compress that evicted range
   size_type total_flushes_; // total number of times this queue has been flushed

   std::set<id_type> returned_; // items < TAIL that were reserved but later returned (not popped)

   boost::ptr_list<waiter> waiters_;
   boost::ptr_list<waiter>::iterator wake_up_it_;

   boost::asio::io_service& ios_;
   const std::string path_;
};

} // darner

#endif // __DARNER_QUEUE_H__
