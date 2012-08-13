#include "darner/net/handler.h"

#include <sstream>
#include <boost/array.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace darner;

handler::handler(io_service& ios,
                 request_parser& parser,
                 queue_map& queues,
                 stats& _stats,
                 queue::size_type chunk_size /* = 1024 */)
   : chunk_size_(chunk_size),
     socket_(ios),
     parser_(parser),
     queues_(queues),
     stats_(_stats),
     in_(chunk_size + 2) // make room for \r\n
{
}

handler::~handler()
{
   ++stats_.conns_closed;
}

void handler::start()
{
   ++stats_.conns_opened;
   socket_.set_option(ip::tcp::no_delay(true));

   read_request(system::error_code(), 0);
}

void handler::read_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      log::ERROR("handler<%1%>::read_request: %2%", shared_from_this(), e.message());
      return done(false);
   }

   async_read_until(
      socket_, in_, '\n',
      bind(&handler::parse_request, shared_from_this(), _1, _2));
}

void handler::parse_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      if (e != error::eof || in_.size()) // clean close by client?
         log::ERROR("handler<%1%>::handle_read_request: %2%", shared_from_this(), e.message());
      return done(false);
   }

   // TODO: it would have been nice to pass in an buffers_iterator directly to spirit, but
   // something constness thing about the iterator_traits::value_type is borking up being able to use it
   asio::streambuf::const_buffers_type bufs = in_.data();
   buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
   if (!parser_.parse(req_, buf_))
      return done(false, "ERROR\r\n");
   in_.consume(bytes_transferred);

   switch (req_.type)
   {
   case request::RT_STATS:     write_stats();   break;
   case request::RT_VERSION:   write_version(); break;
   case request::RT_FLUSH:     flush();         break;
   case request::RT_FLUSH_ALL: flush_all();     break;
   case request::RT_SET:       set();           break;
   case request::RT_GET:       get();           break;
   }
}

void handler::write_stats()
{
   ostringstream oss;
   stats_.write(oss);

   for (queue_map::const_iterator it = queues_.begin(); it != queues_.end(); ++it)
      it->second->write_stats(it->first, oss);

   oss << "END\r\n";
   buf_ = oss.str();

   async_write(socket_, buffer(buf_), bind(&handler::read_request, shared_from_this(), _1, _2));
}

void handler::write_version()
{
   done(true, "VERSION " + string(DARNER_VERSION) + "\r\n");
}

void handler::flush()
{
   // TODO: implement
}

void handler::flush_all()
{
   // TODO: implement
}

void handler::set()
{
   ++stats_.cmd_sets;

   // round up the number of chunks we need, and fetch \r\n if it's just one chunk
   push_stream_ = in_place(ref(queues_[req_.queue]), (req_.num_bytes + chunk_size_ - 1) / chunk_size_);
   queue::size_type remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;

   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::set_on_read_chunk(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      log::ERROR("handler<%1%>::set_on_read_chunk: %2%", shared_from_this(), e.message());
      return done(false);
   }

   asio::streambuf::const_buffers_type bufs = in_.data();
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_->tell();

   if (bytes_remaining <= chunk_size_) // last chunk!  make sure it ends with \r\n
   {
      buf_.assign(buffers_begin(bufs) + bytes_remaining, buffers_begin(bufs) + bytes_remaining + 2);
      if (buf_ != "\r\n")
         return done(false, "CLIENT_ERROR bad data chunk\r\n");
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_remaining);
      in_.consume(bytes_remaining + 2);
   }
   else
   {
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + chunk_size_);
      in_.consume(chunk_size_);
   }

   try
   {
      push_stream_->write(buf_);
   }
   catch (const system::system_error& ex)
   {
      log::ERROR("handler<%1%>::set_on_write_chunk: %2%", shared_from_this(), ex.code().message());
      return done(false, "SERVER_ERROR " + ex.code().message() + "\r\n");
   }

   if (push_stream_->tell() == req_.num_bytes) // are we all done?
   {
      push_stream_.reset();
      return done(true, "STORED\r\n");
   }

   // otherwise, second verse, same as the first
   queue::size_type remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;
   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::get()
{
   ++stats_.cmd_gets;

   if (req_.get_abort && (req_.get_open || req_.get_close))
      return done(false, "CLIENT_ERROR abort must be by itself\r\n");

   if (pop_stream_) // before getting the next item, close this one
   {
      if (!req_.get_close && !req_.get_abort) // needs to be a close or an abort
         return done(false, "CLIENT_ERROR close current item first\r\n");

      try
      {
         pop_stream_->close(req_.get_close);
      }
      catch (const system::system_error& ex)
      {
         log::ERROR("handler<%1%>::get: %2%", shared_from_this(), ex.code().message());
         return done(false, "SERVER_ERROR " + ex.code().message() + "\r\n");
      }

      pop_stream_.reset();
   }

   if (req_.get_abort)
      return done(true, "END\r\n"); // aborts go no further

   pop_stream_ = in_place(ref(queues_[req_.queue]));

   if (!pop_stream_->read(buf_))
   {
      pop_stream_.reset();

      // couldn't read... can we at least wait?
      if (req_.wait_ms)
         queues_[req_.queue].wait(req_.wait_ms, bind(&handler::get_on_queue_return, shared_from_this(), _1));
      else
         return done(true, "END\r\n");
   }
   else
      write_first_chunk();
}

void handler::get_on_queue_return(const boost::system::error_code& e)
{
   if (e == asio::error::timed_out)
      return done(true, "END\r\n");
   else if (e)
   {
      log::ERROR("handler<%1%>::get_on_queue_return: %2%", shared_from_this(), e.message());
      return done(false, "SERVER_ERROR " + e.message() + "\r\n");
   }
   else
   {
      if (!pop_stream_->read(buf_))
      {
         // well this is very unusual.  the queue woke us up but nothing is available.
         pop_stream_.reset();
         log::ERROR("handler<%1%>::get_on_queue_return: %2%", shared_from_this(), "bad queue_return");
         return done(false, "SERVER_ERROR bad queue return\r\n");
      }

      write_first_chunk();
   }
}

void handler::write_first_chunk()
{
   ostringstream oss;
   oss << "VALUE " << req_.queue << " 0 " << pop_stream_->size() << "\r\n";
   header_buf_ = oss.str();
   array<const_buffer, 2> bufs = {{ buffer(header_buf_), buffer(buf_) }};

   async_write(socket_, bufs, bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
}

void handler::get_on_write_chunk(const boost::system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      log::ERROR("handler<%1%>::get_on_write_chunk: %2%", shared_from_this(), e.message());
      return done(false);
   }

   if (pop_stream_->tell() == pop_stream_->size())
   {
      if (!req_.get_open)
      {
         try
         {
            pop_stream_->close(true);
         }
         catch (const system::system_error& ex)
         {
            log::ERROR("handler<%1%>::get_on_write_chunk: %2%", shared_from_this(), ex.code().message());
            return done(false);
         }
         pop_stream_.reset();
      }
      return done(true, "\r\nEND\r\n");
   }
   else
   {
      try
      {
         pop_stream_->read(buf_);
      }
      catch (const system::system_error& ex)
      {
         log::ERROR("handler<%1%>::get_on_write_chunk: %2%", shared_from_this(), ex.code().message());
         return done(false);
      }
      async_write(socket_, buffer(buf_), bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
   }
}

void handler::done(bool success, const std::string& msg /* = "" */)
{
   if (!msg.empty())
   {
      buf_ = msg;
      if (success)
         async_write(socket_, buffer(buf_), bind(&handler::read_request, shared_from_this(), _1, _2));
      else
         async_write(socket_, buffer(buf_), bind(&handler::finalize, shared_from_this(), _1, _2));
   }
   else
   {
      if (success)
         read_request(system::error_code(), 0);
      else
         finalize(system::error_code(), 0);
   }
}

void handler::finalize(const system::error_code& e, size_t bytes_transferred)
{
   if (pop_stream_)
   {
      try
      {
         pop_stream_->close(false);
      }
      catch (const system::system_error& ex)
      {
         log::ERROR("handler<%1%>::finalize: %2%", shared_from_this(), ex.code().message());
      }
   }

   if (push_stream_)
   {
      try
      {
         push_stream_->cancel();
      }
      catch (const system::system_error& ex)
      {
         log::ERROR("handler<%1%>::finalize: %2%", shared_from_this(), ex.code().message());
      }
   }
}
