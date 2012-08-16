#ifndef __DARNER_QUEUE_OQSTREAM_H__
#define __DARNER_QUEUE_OQSTREAM_H__

#include <boost/shared_ptr.hpp>

#include "darner/queue/queue.h"

namespace darner {

class oqstream
{
public:

   /*
    * destroying an unfinished oqstream will cancel it
    */
   ~oqstream();

   /*
    * immediately opens an oqstream for writing.  the stream will automatically close after chunks_count chunks
    * have been written
    */
   void open(boost::shared_ptr<queue> queue, queue::size_type chunks_count);

   /*
    * writes a chunk of the item. fails if more chunks are written than originally reserved.
    */
   void write(const std::string& chunk);

   /*
    * cancels the oqstream write.  only available if the stream hasn't written chunks_count chunks yet
    */
   void cancel();

   /*
    * returns the position in the stream in bytes.
    */
   queue::size_type tell() const { return header_.size; }
   
private:

   boost::shared_ptr<queue> queue_;

   queue::id_type id_; // id of key in queue, only set after all chunks are written
   queue::header_type header_; // only set if it's multi-chunk
   queue::size_type chunk_pos_;
};

} // darner

#endif // __DARNER_QUEUE_OQSTREAM_H__
