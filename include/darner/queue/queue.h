#ifndef __DARNER_QUEUE_H__
#define __DARNER_QUEUE_H__

#include <stdexcept>
#include <set>

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/optional.hpp>
#include <boost/bind.hpp>
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
 * - items are first checked out, then later deleted or returned back into the queue
 * - large items are streamed in a chunk at a time
 *
 * queue will post events such as journal writes and waits to a provided boost::asio io_service.  interrupting the
 * io_service with pending events is okay - queue is never in an inconsistent state between io events.
 *
 * queue is not thread-safe, it assumes a single-thread calling and operating the provided io_service
 */
class queue
{
public:

   typedef boost::uint64_t id_type;
   typedef boost::uint64_t size_type;
   typedef boost::function<void (const boost::system::error_code& error)> success_callback;

   // open or create the queue at the path
   queue(boost::asio::io_service& ios, const std::string& path);

public:

   friend class istream;
   friend class ostream;

   // queue item points to chunk item via a small metadata header
   class header_type
   {
   public:

      header_type() : beg(0), end(0), size(0) {}
      header_type(id_type _beg, id_type _end, size_type _size)
      : beg(_beg), end(_end), size(_size) {}
      header_type(const std::string& buf)
      {
         *this = *reinterpret_cast<const header_type*>(buf.c_str());
      }

      id_type beg;
      id_type end;
      size_type size;

      const std::string& str() const;

   private:

      mutable std::string buf_;
   };

   // queue methods:

   /*
    * pushes a value to to the queue, calls cb after push with a success code, returns true for success,
    * false if there was a problem writing to the journal
    */
   bool push(boost::optional<id_type>& result, const std::string& value);

   /*
    * pushes a header to to the queue, calls cb after push with a success code, returns true for success,
    * false if there was a problem writing to the journal
    */
   bool push(boost::optional<id_type>& result, const header_type& value);

   /*
    * begins the popping of an item.  if the item is a single chunk, pops the value, otherwise just pops the
    * header.  if no items are available, will try to wait wait_ms milliseconds before failing to pop
    */
   void pop_open(boost::optional<id_type>& result_id, boost::optional<header_type>& result_header,
      std::string& result_value, size_type wait_ms, const success_callback& cb);

   /*
    * finishes the popping of an item.  if remove = true, deletes the dang ol' item, otherwise returns it
    * back into the queue
    */
   bool pop_close(bool remove, id_type id, const boost::optional<header_type>& header);

   /*
    * returns the number of items in the queue
    */
   size_type count();

   // TODO: consider also reporting a queue size

   // chunk methods:

   /*
    * returns a header with a range of reserved chunks
    */
   void reserve_chunks(boost::optional<header_type>& result, size_type chunks);

   /*
    * writes a chunk
    */
   bool write_chunk(const std::string& value, id_type chunk_key);

   /*
    * reads a chunk
    */
   bool read_chunk(std::string& result, id_type chunk_key);

   /*
    * removes all chunks referred to by a header
    */
   bool erase_chunks(const header_type& header);

   class key_type
   {
   public:

      enum  { KT_QUEUE = 1, KT_CHUNK = 2 }; // a key can be either a queue or a chunk type

      key_type() : type(KT_QUEUE), id(0) {}

      key_type(char _type, id_type _id) : type(_type), id(_id) {}

      key_type(const leveldb::Slice& s)
      : type(s.data()[sizeof(id_type)]), id(*reinterpret_cast<const id_type*>(s.data())) {}

      leveldb::Slice slice() const;

      int compare(const key_type& other) const;

      unsigned char type;
      id_type id;

   private:

      mutable std::vector<char> buf_;
   };

   // ties a set of results to a deadline timer
   struct waiter
   {
      waiter(boost::asio::io_service& ios, boost::optional<id_type>& _result_id,
         boost::optional<header_type>& _result_header, std::string& _result_value, size_type wait_ms,
         const success_callback& _cb)
      : result_id(_result_id),
        result_header(_result_header),
        result_value(_result_value),
        cb(_cb),
        timer(ios, boost::posix_time::milliseconds(wait_ms))
      {
      }

      boost::optional<id_type>& result_id;
      boost::optional<header_type>& result_header;
      std::string& result_value;
      success_callback cb;

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

   // any operation that mutates the queue or the waiter state should run this to crank any pending events
   void spin_waiters();

   // return true and supply the next queue id if available
   bool next_id(boost::optional<id_type>& id);

   // fires either if timer times out, or is canceled
   void waiter_timeout(const boost::system::error_code& e, boost::ptr_list<waiter>::iterator waiter_it);

   // gets a value and optionally a header if it's multi-chunk
   bool get_item(id_type id, boost::optional<header_type>& header, std::string& value);

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

   std::set<id_type> returned_; // items < TAIL that were reserved but later returned (not popped)

   boost::ptr_list<waiter> waiters_;

   boost::asio::io_service& ios_;
};

} // darner

#endif // __DARNER_QUEUE_H__
