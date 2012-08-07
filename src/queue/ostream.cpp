#include "darner/queue/ostream.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <leveldb/write_batch.h>

using namespace std;
using namespace boost;
using namespace darner;

darner::ostream::ostream(queue& _queue, queue::size_type chunks)
: queue_(_queue),
  chunks_(chunks),
  chunk_pos_(0),
  tell_(0)
{
}

void darner::ostream::write(const std::string& value, const success_callback& cb)
{
   if (id_) // have an id already?  that's a paddlin'
      return cb(asio::error::eof);
   
   if (chunks_ == 1) // just one chunk? push it on
   {
      if (!queue_.push(id_, value))
         return cb(system::error_code(system::errc::io_error, system::system_category()));
      tell_ += value.size();
      return cb(system::error_code());
   }

   if (!header_)
      queue_.reserve_chunks(header_, chunks_);

   if (!queue_.write_chunk(value, chunk_pos_))
      return cb(system::error_code(system::errc::io_error, system::system_category()));
   tell_ += value.size();
   header_->size += value.size();
   if (++chunk_pos_ == header_->end && !queue_.push(id_, *header_))
      return cb(system::error_code(system::errc::io_error, system::system_category()));

   cb(system::error_code());
}

void darner::ostream::cancel(const success_callback& cb)
{
   if (id_) // can't cancel if we already pushed all the chunks
      return cb(asio::error::invalid_argument);

   if (!header_) // nothing wrong with canceling nothing
      return cb(system::error_code());

   if (!queue_.erase_chunks(*header_))
      return cb(system::error_code(system::errc::io_error, system::system_category()));

   header_ = optional<queue::header_type>();
   chunk_pos_ = tell_ = 0;
}

