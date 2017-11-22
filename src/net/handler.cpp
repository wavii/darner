#include "darner/net/handler.h"

#include <cstdio>

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
      return error("read_request", e);

   async_read_until(socket_, in_, '\n', bind(&handler::parse_request, shared_from_this(), _1, _2));
}

void handler::parse_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e == error::eof && !in_.size()) // clean close by client?
      return;
   else if (e)
      return error("parse_request", e);

   // TODO: it would have been nice to pass in an buffers_iterator directly to spirit, but
   // something constness thing about the iterator_traits::value_type is borking up being able to use it
   asio::streambuf::const_buffers_type bufs = in_.data();
   buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
   if (!parser_.parse(req_, buf_))
      return error("");
   in_.consume(bytes_transferred);

   switch (req_.type)
   {
   case request::RT_STATS:     write_stats();   break;
   case request::RT_VERSION:   write_version(); break;
   case request::RT_DESTROY:   destroy();       break;
   case request::RT_FLUSH:     flush();         break;
   case request::RT_FLUSH_ALL: flush_all();     break;
   case request::RT_SET:       ++stats_.cmd_sets; set(); break;
   case request::RT_GET:       ++stats_.cmd_gets; get(); break;
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
   buf_ = "VERSION " DARNER_VERSION "\r\n";
   async_write(socket_, buffer(buf_), bind(&handler::read_request, shared_from_this(), _1, _2));
}

void handler::destroy()
{
   queues_.erase(req_.queue, false);
   return end("DELETED\r\n");
}

void handler::flush()
{
   // TODO: flush should guarantee that an item that's halfway pushed should still appear after
   // the flush.  right now, item will only appear to a client that was waiting to pop before the flush 
   queues_.erase(req_.queue, true);
   return end();
}

void handler::flush_all()
{
   for (queue_map::iterator it = queues_.begin(); it != queues_.end(); ++it)
      queues_.erase(it->first, true);
   return end("Flushed all queues.\r\n");
}

void handler::set()
{
   // round up the number of chunks we need, and fetch \r\n if it's just one chunk
   push_stream_.open(queues_[req_.queue], (req_.num_bytes + chunk_size_ - 1) / chunk_size_, req_.set_sync);
   queue::size_type remaining = req_.num_bytes - push_stream_.tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;

   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::set_on_read_chunk(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
      return error("set_on_read_chunk", e);

   asio::streambuf::const_buffers_type bufs = in_.data();
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_.tell();

   if (bytes_remaining <= chunk_size_) // last chunk!  make sure it ends with \r\n
   {
      buf_.assign(buffers_begin(bufs) + bytes_remaining, buffers_begin(bufs) + bytes_remaining + 2);
      if (buf_ != "\r\n")
         return error("bad data chunk", "CLIENT_ERROR");
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
      push_stream_.write(buf_);
   }
   catch (const system::system_error& ex)
   {
      return error("set_on_read_chunk", ex);
   }

   if (push_stream_.tell() == req_.num_bytes) // are we all done?
   {
      ++stats_.items_enqueued;
      return end("STORED\r\n");
   }

   // otherwise, second verse, same as the first
   queue::size_type remaining = req_.num_bytes - push_stream_.tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;
   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::get()
{
   if (req_.get_abort && (req_.get_open || req_.get_close || req_.get_peek))
      return error("abort must be by itself", "CLIENT_ERROR");

   if (req_.get_peek && req_.get_open)
      return error("cannot open and peek", "CLIENT_ERROR");

   if (req_.get_abort || req_.get_close)
   {
      try
      {
         pop_stream_.close(req_.get_close);
      }
      catch (const system::system_error& ex)
      {
         return error("get", ex);
      }
   }
   else if (pop_stream_)
      return error("close current item first", "CLIENT_ERROR");

   if ((req_.get_close && !req_.get_open) || req_.get_abort)
      return end(); // closes/aborts go no further

   if (!pop_stream_.open(queues_[req_.queue]))
   {
      if (req_.wait_ms) // couldn't read... can we at least wait?
         return queues_[req_.queue]->wait(req_.wait_ms, bind(&handler::get_on_queue_return, shared_from_this(), _1));
      else
         return end();
   }

   try
   {
      pop_stream_.read(buf_);
   }
   catch (const system::system_error& ex)
   {
      return error("get", ex);
   }

   header_buf_.resize(21 + req_.queue.size()); // 21 = len("VALUE  0 4294967296\r\n")
   header_buf_.resize(::sprintf(&header_buf_[0], "VALUE %s 0 %lu\r\n", req_.queue.c_str(), pop_stream_.size()));

   if (pop_stream_.tell() == pop_stream_.size())
   {
      if (!req_.get_open)
      {
         try
         {
            pop_stream_.close(!req_.get_peek);
         }
         catch (const system::system_error& ex)
         {
            return error("get_on_write_chunk", ex, false);
         }
      }
      ++stats_.items_dequeued;
      boost::array<const_buffer, 3> bufs = {{ buffer(header_buf_), buffer(buf_), buffer("\r\nEND\r\n", 7) }};
      async_write(socket_, bufs, bind(&handler::read_request, shared_from_this(), _1, _2));
   }
   else
   {
      boost::array<const_buffer, 2> bufs = {{ buffer(header_buf_), buffer(buf_) }};
      async_write(socket_, bufs, bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
   }
}

void handler::get_on_queue_return(const boost::system::error_code& e)
{
   if (e == asio::error::timed_out)
      return end();
   else if (e)
      return error("get_on_queue_return", e);
   else
      get();
}

void handler::get_on_write_chunk(const boost::system::error_code& e, size_t bytes_transferred)
{
   if (e)
      return error("get_on_write_chunk", e, false);

   if (pop_stream_.tell() == pop_stream_.size())
   {
      if (!req_.get_open)
      {
         try
         {
            pop_stream_.close(!req_.get_peek);
         }
         catch (const system::system_error& ex)
         {
            return error("get_on_write_chunk", ex, false);
         }
      }
      ++stats_.items_dequeued;

      return end("\r\nEND\r\n");
   }
   else
   {
      try
      {
         pop_stream_.read(buf_);
      }
      catch (const system::system_error& ex)
      {
         return error("get_on_write_chunk", ex, false);
      }

      async_write(socket_, buffer(buf_), bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
   }
}
