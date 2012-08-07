#ifndef __DARNER_QUEUE_IQSTREAM_H__
#define __DARNER_QUEUE_IQSTREAM_H__

#include <boost/optional.hpp>
#include <boost/function.hpp>

#include "darner/queue/queue.h"

namespace darner {

class iqstream
{
public:

   typedef boost::function<void (const boost::system::error_code& error)> success_callback;

   iqstream(queue& _queue, queue::size_type wait_ms);

   /*
    * on the first read, waits for an item to enter the queue, and calls cb after at most wait_ms milliseconds
    * with a success code.  after a successful first read, you can continue reading until eof
    * (or until tell() == size()).  lifetime of string result until cb is called is the responsibility of the caller.
    */
   void read(std::string& result, const success_callback& cb);

   /*
    * closes the iqstream.  if remove, completes the pop of the item off the queue, otherwise returns it
    */
   void close(bool remove, const success_callback& cb);

   /*
    * returns the position in the stream in bytes.  only valid after first read()
    */
   queue::size_type tell() const { return tell_; }

   /*
    * returns the size of the item.  only valid after first read()
    */
    queue::size_type size() const { return header_ ? header_->size : tell_; }
   
private:

   void on_open(std::string& result, const success_callback& cb, const boost::system::error_code& e);

   queue& queue_;
   queue::size_type wait_ms_;

   boost::optional<queue::id_type> id_; // id of key in queue, only set after a succesful first read
   boost::optional<queue::header_type> header_; // only set if it's multi-chunk
   queue::size_type chunk_pos_;
   queue::size_type tell_;
};

} // darner

#endif // __DARNER_QUEUE_IQSTREAM_H__
