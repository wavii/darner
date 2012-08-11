#include "darner/queue/iqstream.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <leveldb/write_batch.h>

using namespace std;
using namespace boost;
using namespace darner;

iqstream::iqstream(queue& _queue)
: queue_(_queue),
  chunk_pos_(0),
  tell_(0)
{
}

bool iqstream::read(string& result)
{
   if (!id_) // should we try to fetch a queue item?
   {
      if (!queue_.pop_open(id_, header_, result))
         return false;

      if (!header_) // not multi-chunk?  we're done!
      {
         tell_ += result.size();
         return true;
      }

      chunk_pos_ = header_->beg;
   }

   // if we have an id already, and are still requesting more reads, we must be multi-chunk
   // make sure that's the cast, and that we haven't read past the end
   if (!header_ || chunk_pos_ >= header_->end)
      throw system::system_error(asio::error::eof);

   queue_.read_chunk(result, chunk_pos_);

   ++chunk_pos_;
   tell_ += result.size();
}

void iqstream::close(bool remove)
{
   if (!id_)
      throw system::system_error(asio::error::not_found); // can't close something we haven't opened

   queue_.pop_close(remove, *id_, header_);
}
