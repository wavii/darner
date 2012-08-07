#include "darner/queue/istream.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <leveldb/write_batch.h>

using namespace std;
using namespace boost;
using namespace darner;

darner::istream::istream(queue& _queue, queue::size_type wait_ms)
: queue_(_queue),
  wait_ms_(wait_ms),
  chunk_pos_(0),
  tell_(0)
{
}

void darner::istream::read(string& result, const success_callback& cb)
{
   if (id_) // have an id already?  probably just reading the next chunk
   {
      if (!header_ || chunk_pos_ >= header_->end)
         return cb(asio::error::eof);
      if (!queue_.read_chunk(result, chunk_pos_))
         return cb(system::error_code(system::errc::io_error, system::system_category()));
      ++chunk_pos_;
      tell_ += result.size();
   }
   else
      queue_.pop_open(id_, header_, result, wait_ms_, bind(&darner::istream::on_open, this, ref(result), cref(cb), _1));
}

void darner::istream::close(bool remove, const success_callback& cb)
{
   if (!id_)
      return cb(asio::error::not_found);
   if (!queue_.pop_close(remove, *id_, header_))
      return cb(system::error_code(system::errc::io_error, system::system_category()));
   cb(system::error_code());
}

void darner::istream::on_open(string& result, const success_callback& cb, const system::error_code& e)
{
   if (e)
      return cb(e);
   if (header_)
   {
      chunk_pos_ = header_->beg;
      if (!queue_.read_chunk(result, chunk_pos_))
         return cb(system::error_code(system::errc::io_error, system::system_category()));
      ++chunk_pos_;
   }
   tell_ += result.size();
   cb(system::error_code());
}
