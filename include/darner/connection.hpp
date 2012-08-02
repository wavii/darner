#ifndef __DARNER_CONNECTION_HPP__
#define __DARNER_CONNECTION_HPP__

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

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

   void handle_read_request(const boost::system::error_code& error,
                            size_t bytes_transferred)
   {

   }

   socket_type socket_;
   boost::asio::streambuf buf_;
};

} // darner

#endif // __DARNER_CONNECTION_HPP__
