#ifndef __DARNER_CONNECTION_HPP__
#define __DARNER_CONNECTION_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "darner/request.hpp"
#include "darner/log.h"

namespace darner {

class connection : public boost::enable_shared_from_this<connection>
{
public:

   typedef boost::asio::ip::tcp::socket socket_type;
   typedef boost::shared_ptr<connection> ptr_type;

   connection(boost::asio::io_service& ios,
              size_t max_frame_size = 4096)
   : socket_(ios),
     buf_(max_frame_size)
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
         buf_,
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
      if (e)
      {
         if (e != boost::asio::error::eof) // clean close by client
            log::ERROR("connection::handle_read_request: %1%", e.message());
         return;
      }

      good_request_ = parser_.parse(req_, std::string((std::istreambuf_iterator<char>(&buf_)),
         std::istreambuf_iterator<char>()));

      const char* response = good_request_ ? "OK\r\n" : "ERROR\r\n";
      buf_.consume(buf_.size());

      boost::asio::async_write(
         socket_,
         boost::asio::buffer(response),
         boost::bind(&connection::handle_write_response,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
   }

   void handle_write_response(const boost::system::error_code& e,
                              size_t bytes_transferred)
   {
      if (e)
      {
         log::ERROR("connection::handle_write_response: %1%", e.message());
         return;
      }

      if (good_request_)
         boost::asio::async_read_until(
            socket_,
            buf_,
            '\n',
            boost::bind(&connection::handle_read_request,
               shared_from_this(),
               boost::asio::placeholders::error,
               boost::asio::placeholders::bytes_transferred));
   }

   socket_type socket_;
   boost::asio::streambuf buf_;
   request req_;
   request_parser<std::string::const_iterator> parser_;
   bool good_request_;
};

} // darner

#endif // __DARNER_CONNECTION_HPP__
