#include "darner/queue/oqstream.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <leveldb/write_batch.h>

using namespace std;
using namespace boost;
using namespace darner;

oqstream::oqstream(queue& _queue, queue::size_type chunks)
: queue_(_queue),
  chunks_(chunks),
  chunk_pos_(0),
  tell_(0)
{
}

void oqstream::write(const std::string& value)
{
   if (id_) // have an id already?  that's a paddlin'
      throw system::system_error(asio::error::eof);
   
   if (chunks_ == 1) // just one chunk? push it on
   {
      queue_.push(id_, value);
      tell_ += value.size();
      return;
   }

   if (!header_)  // reserve a swath of chunks if we haven't already
      queue_.reserve_chunks(header_, chunks_);

   queue_.write_chunk(value, chunk_pos_);

   tell_ += value.size();
   header_->size += value.size();

   if (++chunk_pos_ == header_->end) // time to push the item?
      queue_.push(id_, *header_);
}

void oqstream::cancel()
{
   if (id_) // can't cancel if we already pushed all the chunks
      throw system::system_error(asio::error::invalid_argument);

   if (!header_) // nothing wrong with canceling nothing
      return;

   queue_.erase_chunks(*header_);

   header_.reset();
   chunk_pos_ = tell_ = 0;
}

