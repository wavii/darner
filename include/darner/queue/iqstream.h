#ifndef __DARNER_QUEUE_IQSTREAM_H__
#define __DARNER_QUEUE_IQSTREAM_H__

#include <boost/shared_ptr.hpp>

#include "darner/queue/queue.h"

namespace darner {

class iqstream
{
public:

   /*
    * destroying an open iqstream will close it with erase = false
    */
   ~iqstream();

   /*
    * tries to open an item for reading.  returns true if an item was available.
    */
   bool open(boost::shared_ptr<queue> queue);

   /*
    * reads a chunk
    * can continue calling read until eof (until tell() == size()).
    */
   void read(std::string& result);

   /*
    * closes the iqstream.  if erase, completes the pop of the item off the queue, otherwise returns it.
    */
   void close(bool erase);

   /*
    * returns the position in the stream in bytes.  only valid after first read()
    */
   queue::size_type tell() const { return tell_; }

   /*
    * returns the size of the item.  only valid after first read()
    */
   queue::size_type size() const { return header_.size; }

   /*
    * returns true if open
    */
   operator bool() const { return static_cast<bool>(queue_); }
   
private:

   boost::shared_ptr<queue> queue_;

   queue::id_type id_; // id of key in queue, only valid if open() succeeded
   queue::header_type header_; // only valid if it's multi-chunk
   queue::size_type chunk_pos_;
   queue::size_type tell_;
};

} // darner

#endif // __DARNER_QUEUE_IQSTREAM_H__
