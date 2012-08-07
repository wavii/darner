#include "darner/net/handler.h"

#include <sstream>

using namespace std;
using namespace boost;
using namespace darner;

handler::handler(asio::io_service& ios,
                 request_parser& parser,
                 queue_map& queues,
                 stats& _stats,
                 size_t chunk_size /* = 4096 */)
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
   file_ = file_type();
   bytes_remaining_ = req_.num_bytes;
   size_type to_read = bytes_remaining_ > chunk_size_ ? chunk_size_ : bytes_remaining_ + 2; // last chunk? get \r\n too
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
      if (file_.header.size) // have a file?  nix it!
         queues_.get_io_service().post(
            bind(
               &queue::push_cancel, &queues_[req_.queue], ref(file_), socket_.get_io_service().wrap(
               bind(
                  &handler::do_nothing, shared_from_this(), _1
               ))));

      return log::ERROR("handler<%1%>::on_set_read_chunk: %2%", shared_from_this(), e.message());
   }

   asio::streambuf::const_buffers_type bufs = in_.data();
   if (bytes_remaining_ <= chunk_size_) // last chunk!  make sure it ends with \r\n
   {
      buf_.assign(buffers_begin(bufs) + bytes_remaining_, buffers_begin(bufs) + bytes_remaining_ + 2);
      if (buf_ != "\r\n")
         return write_result(false, "CLIENT_ERROR bad data chunk\r\n");
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_remaining_);
      in_.consume(bytes_remaining_ + 2);
   }
   else
   {
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + chunk_size_);
      in_.consume(chunk_size_);
   }

   // three cases
   if (req_.num_bytes <= chunk_size_) // simple single-chunk push
   {
      queues_.get_io_service().post(
         bind(
            &queue::push, &queues_[req_.queue], cref(buf_), socket_.get_io_service().wrap(
               bind(
                  &handler::set_on_push_value, shared_from_this(), _1, _2
               ))));
   }
   else if (!file_.header.size) // first chunk, allocate a file
   {
      size_type num_chunks = (req_.num_bytes + chunk_size_ - 1) / chunk_size_; // round up
      // first chunk, get a file
      queues_.get_io_service().post(
         bind(
            &queue::push_chunk, &queues_[req_.queue], ref(file_), num_chunks, cref(buf_), socket_.get_io_service().wrap(
               bind(
                  &handler::set_on_push_value, shared_from_this(), _1, _2
               ))));
   }
   else // later chunk, use the allocated file
   {
      queues_.get_io_service().post(
         bind(
            &queue::push_chunk, &queues_[req_.queue], ref(file_), cref(buf_), socket_.get_io_service().wrap(
               bind(
                  &handler::set_on_push_value, shared_from_this(), _1, _2
               ))));
   }
}

void handler::set_on_push_value(const boost::system::error_code& e, const file_type& file)
{
   if (e)
   {
      if (file_.header.size) // have a file?  nix it!
         queues_.get_io_service().post(
            bind(
               &queue::push_cancel, &queues_[req_.queue], ref(file_), socket_.get_io_service().wrap(
               bind(
                  &handler::do_nothing, shared_from_this(), _1
               ))));
      return write_result(false, ("SERVER_ERROR " + e.message() + "\r\n").c_str());
   }

   if (file_.tell == file_.header.chunk_end) // all done!
      return write_result(true, "STORED\r\n");

   bytes_remaining_ = req_.num_bytes - file_.header.size;
   size_type to_read = bytes_remaining_ > chunk_size_ ? chunk_size_ : bytes_remaining_ + 2; // last chunk? get \r\n too
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
