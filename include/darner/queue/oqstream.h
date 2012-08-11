#ifndef __DARNER_QUEUE_OQSTREAM_H__
#define __DARNER_QUEUE_OQSTREAM_H__

#include <boost/optional.hpp>
#include <boost/function.hpp>

#include "darner/queue/queue.h"

namespace darner {

class oqstream
{
public:

   oqstream(queue& _queue, queue::size_type chunks);

   /*
    * writes a chunk of the item. fails if more chunks are written than originally reserved.
    */
   void write(const std::string& value);

   /*
    * cancels the oqstream write.  only available to mutli-chunks that haven't written all their chunks yet.
    */
   void cancel();

   /*
    * returns the position in the stream in bytes.
    */
   queue::size_type tell() const { return tell_; }
   
private:

   queue& queue_;
   queue::size_type chunks_;

   boost::optional<queue::id_type> id_; // id of key in queue, only set after all chunks are written
   boost::optional<queue::header_type> header_; // only set if it's multi-chunk
   queue::size_type chunk_pos_;
   queue::size_type tell_;
};

} // darner

#endif // __DARNER_QUEUE_OQSTREAM_H__
