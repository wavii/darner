#ifndef __DARNER_QUEUE_HPP__
#define __DARNER_QUEUE_HPP__

#include <stdexcept>
#include <set>

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/comparator.h>

#include "darner/queue/format.hpp"

namespace darner {

/*
 * queue is a fifo queue that is O(log(queue size / cache size)) for pushing/popping.  it boasts these features:
 *
 * - no blocking calls for the caller - blocking i/o can run on a separate i/o service thread
 * - an evented wait semantic for queue poppers
 * - items are first checked out, then later deleted or returned back into the queue
 * - large items are streamed in a chunk at a time
 *
 * queue will post events such as journal writes to a provided boost::asio io_service.  interrupting the
 * io_service with pending events is okay - queue is never in an inconsistent state between io events.
 *
 * queue is not thread-safe, it assumes a single-thread calling and operating the provided io_service
 */
class queue
{
public:

   typedef boost::function<void (const boost::system::error_code& error)> success_callback;
   typedef boost::function<void (const boost::system::error_code& error, const file_type& file)> push_callback;
   typedef boost::function<void (const boost::system::error_code& error, const file_type& file,
      std::string& value)> pop_callback;

   // open or create the queue at the path
   queue(boost::asio::io_service& ios, const std::string& path);

   /*
    * push a single-chunk value into the queue.  calls cb after the write with a success code. this method does not
    * manage the lifetime of the value passed to it - the caller must leave the value unchanged until after cb is
    * called.
    */
   void push(const std::string& value, const push_callback& cb);

   /*
    * reserve chunks and stores the reservation in file, and pushes the first chunk
    */
   void push_chunk(file_type& file, size_type chunks, const std::string& value, const push_callback& cb);

   /*
    * pushes a chunk of a multi-chunk value into the queue.  as soon as the final chunk is pushed, the item
    * becomes available for popping.
    */
   void push_chunk(file_type& file, const std::string& value, const push_callback& cb);

   /*
    * cancels a multi-chunk push; deletes all the chunks
    */
   void push_cancel(const file_type& file, const success_callback& cb);

   /*
    * reserves an item for popping off the back of the queue.  calls cb after at most wait_ms milliseconds with a
    * success code, the item's key, and the item's value.  on failure, sets error as either timed_out if no
    * items were available after wait_ms milliseconds, or io_error if there was a problem with the underlying journal
    */
   void pop(size_t wait_ms, const pop_callback& cb);

   /*
    * reads the next chunk from an item to be popped
    */
   void pop_chunk(file_type& file, const pop_callback& cb);

   /*
    * finish the pop, either by deleting the item or returning back to the queue.  calls cb after the pop_end finishes
    * with a success code. on failure, sets error as io_error if there was a problem with the underlying journal.
    */
   void pop_end(const file_type& file, bool remove, const success_callback& cb);

   /*
    * returns the number of items in the queue
    */
   size_type count();

   // TODO: consider also reporting a queue size
   
private:

   // a waiter is a cheap struct that ties a callback to a deadline timer
   struct waiter
   {
      waiter(boost::asio::io_service& ios, size_t wait_ms, const pop_callback& _cb)
      : cb(_cb),
        timer(ios, boost::posix_time::milliseconds(wait_ms))
      {
      }

      pop_callback cb;
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

   // fetch the next key and return true if there is one
   bool next_key(file_type & file);

   // fetch the value in the journal at key and pass it to cb
   void get_value(file_type& file, const pop_callback& cb);

   // put the value into the journal, update file with the put location, and pass it onto cb
   void put_value(file_type& file, const std::string& value, const push_callback& cb);

   // a waiter timed out or was canceled
   void waiter_timeout(const boost::system::error_code& error, boost::ptr_list<waiter>::iterator waiter_it);

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

#endif // __DARNER_QUEUE_HPP__
