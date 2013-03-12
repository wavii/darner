#include "darner/queue/oqstream.h"

#include <stdexcept>

#include <boost/asio.hpp>

#include "darner/util/log.h"

using namespace std;
using namespace boost;
using namespace darner;

oqstream::~oqstream()
{
   if (queue_)
   {
      try
      {
         cancel();
      }
      catch (const std::exception& e) // we're in the dtor!  we have to swallow everything
      {
         log::ERROR("oqstream::~oqstream(): %1%", e.what());
      }
      catch (...)
      {
         log::ERROR("oqstream::~oqstream(): unknown error");
      }
   }
}

void oqstream::open(boost::shared_ptr<queue> queue, queue::size_type chunks_count, bool sync)
{
   if (queue_) // already open?  that's a paddlin'
      throw system::system_error(asio::error::already_open);

   sync_ = sync;
   queue_ = queue;
   header_ = queue::header_type();
   if (chunks_count > 1)
      queue_->reserve_chunks(header_, chunks_count);
   chunk_pos_ = header_.beg;
}

void oqstream::write(const std::string& chunk)
{
  if (!queue_ || chunk_pos_ == header_.end)
      throw system::system_error(asio::error::eof);

   if (header_.end <= 1) // just one chunk? push it on
      queue_->push(id_, chunk, sync_);
   else
      queue_->write_chunk(chunk, chunk_pos_);

   header_.size += chunk.size();

   if (++chunk_pos_ == header_.end) // time to close up shop?
   {
      if (header_.end > 1) // multi-chunk?  push the header
         queue_->push(id_, header_, sync_);
      queue_.reset();
   }
}

void oqstream::cancel()
{
   if (!queue_)
      throw system::system_error(asio::error::eof); // not gon' do it

   if (header_.end > 1) // multi-chunk? erase them
      queue_->erase_chunks(header_);

   queue_.reset();
}

