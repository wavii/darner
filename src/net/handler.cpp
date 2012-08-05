#include "darner/net/handler.h"

#include <sstream>

using namespace std;
using namespace boost;
using namespace darner;

handler::handler(asio::io_service& ios,
                 request_parser& parser,
                 stats& _stats,
                 size_t max_frame_size /* = 4096 */)
   : socket_(ios),
     parser_(parser),
     stats_(_stats),
     in_buf_(max_frame_size)
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
      return log::ERROR("handler::handle_read_request: %1%", e.message());

   asio::async_read_until(
      socket_,
      in_buf_,
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
         log::ERROR("handler::handle_read_request: %1%", e.message());
      return;
   }

   // TODO: it would have been nice to pass in an asio::buffers_iterator directly to spirit, but
   // something constness thing about the iterator_traits::value_type is borking up being able to use it
   asio::streambuf::const_buffers_type bufs = in_buf_.data();
   string header(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
   if (!parser_.parse(req_, header))
   {
      out_buf_ = "ERROR\r\n";
      return async_write(socket_, asio::buffer(out_buf_),
         bind(&handler::do_nothing,
            shared_from_this(),
            asio::placeholders::error,
            asio::placeholders::bytes_transferred));
   }
   in_buf_.consume(bytes_transferred);

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
      stats_.write(out_buf_);
   }
   async_write(
      socket_,
      asio::buffer(out_buf_),
      bind(&handler::read_request,
         shared_from_this(),
         asio::placeholders::error,
         asio::placeholders::bytes_transferred));
}

void handler::write_version()
{
   ostringstream oss;
   oss << "VERSION " << DARNER_VERSION << "\r\n";
   out_buf_ = oss.str();
   async_write(
      socket_,
      asio::buffer(out_buf_),
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

}

void handler::get()
{

}

void handler::do_nothing(const system::error_code& e, size_t bytes_transferred)
{
}
