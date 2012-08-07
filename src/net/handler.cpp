#include "darner/net/handler.h"

#include <sstream>

using namespace std;
using namespace boost;
using namespace darner;

handler::handler(asio::io_service& ios,
                 request_parser& parser,
                 queue_map& queues,
                 stats& _stats,
                 queue::size_type chunk_size /* = 4096 */)
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
   mutex::scoped_lock lock(stats_.mutex);
   ++stats_.conns_closed;
}

void handler::start()
{
   {
      mutex::scoped_lock lock(stats_.mutex);
      ++stats_.conns_opened;
   }
   socket_.set_option(asio::ip::tcp::no_delay(true));

   read_request(system::error_code(), 0);
}

void handler::read_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
      return log::ERROR("handler<%1%>::handle_read_request: %2%", shared_from_this(), e.message());

   asio::async_read_until(
      socket_,
      in_,
      '\n',
      bind(&handler::parse_request,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::parse_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      if (e != asio::error::eof) // clean close by client
         log::ERROR("handler<%1%>::handle_read_request: %2%", shared_from_this(), e.message());
      return;
   }

   // TODO: it would have been nice to pass in an asio::buffers_iterator directly to spirit, but
   // something constness thing about the iterator_traits::value_type is borking up being able to use it
   asio::streambuf::const_buffers_type bufs = in_.data();
   buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
   if (!parser_.parse(req_, buf_))
      return write_result(false, "ERROR\r\n");
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
   {
      mutex::scoped_lock lock(stats_.mutex);
      stats_.write(buf_);
   }
   asio::async_write(
      socket_,
      asio::buffer(buf_),
      bind(&handler::read_request,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::write_version()
{
   ostringstream oss;
   oss << "VERSION " << DARNER_VERSION << "\r\n";
   buf_ = oss.str();
   asio::async_write(
      socket_,
      asio::buffer(buf_),
      bind(&handler::read_request,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::flush()
{

}

void handler::flush_all()
{

}

void handler::set()
{
   // round up the number of chunks we need, and fetch \r\n if it's just one chunk
   push_stream_ = in_place(ref(queues_[req_.queue]), (req_.num_bytes + chunk_size_ - 1) / chunk_size_);
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type to_read = bytes_remaining > chunk_size_ ? chunk_size_ : bytes_remaining + 2;
   asio::async_read(
      socket_,
      in_,
      asio::transfer_at_least(to_read - in_.size()),
      bind(&handler::set_on_read_chunk,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::set_on_read_chunk(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      queues_.get_io_service().post(bind(
         &oqstream::cancel, push_stream_, oqstream::success_callback(bind(
            &handler::do_nothing, shared_from_this(), _1))));

      return log::ERROR("handler<%1%>::on_set_read_chunk: %2%", shared_from_this(), e.message());
   }

   asio::streambuf::const_buffers_type bufs = in_.data();
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_->tell();
   if (bytes_remaining <= chunk_size_) // last chunk!  make sure it ends with \r\n
   {
      buf_.assign(buffers_begin(bufs) + bytes_remaining, buffers_begin(bufs) + bytes_remaining + 2);
      if (buf_ != "\r\n")
         return write_result(false, "CLIENT_ERROR bad data chunk\r\n");
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_remaining);
      in_.consume(bytes_remaining + 2);
   }
   else
   {
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + chunk_size_);
      in_.consume(chunk_size_);
   }

   queues_.get_io_service().post(bind(
      &oqstream::write, &*push_stream_, cref(buf_), socket_.get_io_service().wrap(bind(
         &handler::set_on_push_value, shared_from_this(), _1))));
}

void handler::set_on_push_value(const boost::system::error_code& e)
{
   if (e)
   {
      queues_.get_io_service().post(bind(
         &oqstream::cancel, push_stream_, oqstream::success_callback(bind(
            &handler::do_nothing, shared_from_this(), _1))));

      log::ERROR("handler<%1%>::on_set_read_chunk: %2%", shared_from_this(), e.message());
      return write_result(false, ("SERVER_ERROR " + e.message() + "\r\n").c_str());
   }

   if (push_stream_->tell() == req_.num_bytes) // all done!
      return write_result(true, "STORED\r\n");

   // second verse, same as the first
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type to_read = bytes_remaining > chunk_size_ ? chunk_size_ : bytes_remaining + 2;
   asio::async_read(
      socket_,
      in_,
      asio::transfer_at_least(to_read - in_.size()),
      bind(&handler::set_on_read_chunk,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::get()
{
}

void handler::do_nothing(const system::error_code& e, size_t bytes_transferred)
{
}

void handler::do_nothing(const system::error_code& e)
{
}

void handler::write_result(bool success, const char* msg)
{
   if (success)
      asio::async_write(socket_, asio::buffer(msg, strlen(msg)),
         bind(&handler::read_request,
            shared_from_this(),
            asio::placeholders::error,
            asio::placeholders::bytes_transferred));
   else
      asio::async_write(socket_, asio::buffer(msg, strlen(msg)),
         bind(&handler::do_nothing,
            shared_from_this(),
            asio::placeholders::error,
            asio::placeholders::bytes_transferred));      
}
