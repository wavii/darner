#include "darner/queue/iqstream.h"

#include <boost/asio.hpp>

using namespace std;
using namespace boost;
using namespace darner;

iqstream::~iqstream()
{
   if (queue_)
      queue_->pop_end(false, id_, header_);
}

bool iqstream::open(boost::shared_ptr<queue> queue)
{
   if (queue_)
      throw system::system_error(asio::error::already_open); // can't open what's open

   if (!queue->pop_begin(id_))
      return false;

   queue_ = queue;
   header_ = queue::header_type();
   chunk_pos_ = header_.beg;
   tell_ = 0;

   return true;
}

void iqstream::read(string& result)
{
   // not open or already read past end
   if (!queue_ || chunk_pos_ >= header_.end)
      throw system::system_error(asio::error::eof);

   if (!chunk_pos_) // first read?  check if there's a header
   {
      queue_->pop_read(result, header_, id_);
      if (header_.end > 1)
         chunk_pos_ = header_.beg;
      else
         header_.size = result.size();
   }

   if (header_.end > 1) // multi-chunk?  get the next chunk!
      queue_->read_chunk(result, chunk_pos_);

   ++chunk_pos_;
   tell_ += result.size();
}

void iqstream::close(bool erase)
{
   if (!queue_)
      return; // it's not an error to close more than once

   queue_->pop_end(erase, id_, header_);
   queue_.reset();
}
