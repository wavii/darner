#ifndef __DARNER_CONNECTION_HPP__
#define __DARNER_CONNECTION_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "darner/util/log.h"
#include "darner/net/request.h"

namespace darner {

class connection : public boost::enable_shared_from_this<connection>
{
public:

   typedef boost::asio::ip::tcp::socket socket_type;
   typedef boost::shared_ptr<connection> ptr_type;

   connection(boost::asio::io_service& ios,
              request_parser& parser,
              size_t max_frame_size = 4096)
   : socket_(ios),
     parser_(parser),
     in_buf_(max_frame_size)
   {
   }

   socket_type& socket()
   {
      return socket_;
   }

   // read a frame into the buffer
   void start()
   {
      socket_.set_option(boost::asio::ip::tcp::no_delay(true));
      boost::asio::async_read_until(
         socket_,
         in_buf_,
         '\n',
         boost::bind(&connection::handle_read_request,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
   }

private:

   void handle_read_request(const boost::system::error_code& e,
                            size_t bytes_transferred)
   {
      using namespace boost::asio;

      if (e)
      {
         if (e != error::eof) // clean close by client
            log::ERROR("connection::handle_read_request: %1%", e.message());
         return;
      }

      // TODO: it would have been nice to pass in an asio::buffers_iterator directly to boost::spirit, but
      // something constness thing about the iterator_traits::value_type is borking up being able to use it
      streambuf::const_buffers_type bufs = in_buf_.data();
      std::string header(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
      good_ = parser_.parse(req_, header);
      in_buf_.consume(bytes_transferred);

      if (!good_)
      {
         std::ostream(&out_buf_) << "ERROR\r\n";
         async_write(
            socket_,
            out_buf_,
            boost::bind(&connection::handle_write_result,
               shared_from_this(),
               boost::asio::placeholders::error,
               boost::asio::placeholders::bytes_transferred));
         return;
      }

      switch (req_.type)
      {
      case request::RT_STATS:
         handle_stats_request();
         break;
      case request::RT_VERSION:
         break;
      }
   }

   void handle_stats_request()
   {

   }

   void handle_write_result(const boost::system::error_code& e,
                            size_t bytes_transferred)
   {
      if (e)
         log::ERROR("connection<%1%>::handle_write_result: ", shared_from_this(), e.message());
      else if (good_)
      {
         out_buf_.consume(bytes_transferred);
         boost::asio::async_read_until(
            socket_,
            in_buf_,
            '\n',
            boost::bind(&connection::handle_read_request,
               shared_from_this(),
               boost::asio::placeholders::error,
               boost::asio::placeholders::bytes_transferred));
      }
   }

   socket_type socket_;
   request_parser& parser_;
   boost::asio::streambuf in_buf_;
   boost::asio::streambuf out_buf_;
   request req_;
   bool good_;
};

} // darner

#endif // __DARNER_CONNECTION_HPP__
