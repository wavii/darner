#ifndef __DARNER_QUEUE_IQSTREAM_H__
#define __DARNER_QUEUE_IQSTREAM_H__

#include <boost/optional.hpp>

#include "darner/queue/queue.h"

namespace darner {

class iqstream
{
public:

   /*
    * tries to open an item immediately.  reads will fail if it couldn't open an item.
    */
   iqstream(queue& _queue);

   /*
    * on the first read, tries to fetch an item and returns true if one was available
    * can continue calling read until eof (until tell() == size()).
    */
   bool read(std::string& result);

   /*
    * closes the iqstream.  if remove, completes the pop of the item off the queue, otherwise returns it
    */
   void close(bool remove);

   /*
    * returns the position in the stream in bytes.  only valid after first read()
    */
   queue::size_type tell() const { return tell_; }

   /*
    * returns the size of the item.  only valid after first read()
    */
    queue::size_type size() const { return header_ ? header_->size : tell_; }
   
private:

   queue& queue_;

   boost::optional<queue::id_type> id_; // id of key in queue, only set after a succesful first read
   boost::optional<queue::header_type> header_; // only set if it's multi-chunk
   queue::size_type chunk_pos_;
   queue::size_type tell_;
};

} // darner

#endif // __DARNER_QUEUE_IQSTREAM_H__
