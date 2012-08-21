#ifndef __DARNER_HANDLER_HPP__
#define __DARNER_HANDLER_HPP__

#include <sstream>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "darner/util/log.h"
#include "darner/util/stats.hpp"
#include "darner/queue/iqstream.h"
#include "darner/queue/oqstream.h"
#include "darner/util/queue_map.hpp"
#include "darner/net/request.h"

namespace darner {

class handler : public boost::enable_shared_from_this<handler>
{
public:

   typedef boost::asio::ip::tcp::socket socket_type;
   typedef boost::shared_ptr<handler> ptr_type;

   handler(boost::asio::io_service& ios, request_parser& parser, queue_map& queues, stats& _stats,
      queue::size_type chunk_size = 1024);

   ~handler();

   socket_type& socket()
   {
      return socket_;
   }

   // start the handler event loop - read the first request
   void start();

private:

   // read from the socket up until a newline (request delimter)
   void read_request(const boost::system::error_code& e, size_t bytes_transferred);

   // parses and routes a request
   void parse_request(const boost::system::error_code& e, size_t bytes_transferred);

   // all the ops:

   void write_stats();

   void write_version();

   void flush();

   void flush_all();

   void set();

   void get();

   // set loop:

   void set_on_read_chunk(const boost::system::error_code& e, size_t bytes_transferred);

   // get loop:

   void get_on_queue_return(const boost::system::error_code& e);

   void write_first_chunk();

   void get_on_read_next_chunk(const boost::system::error_code& e);

   void get_on_write_chunk(const boost::system::error_code& e, size_t bytes_transferred);

   void get_on_pop_close_post(const boost::system::error_code& e);

   // utils

   void end(const char* msg = "END\r\n")
   {
      boost::asio::async_write(
         socket_, boost::asio::buffer(std::string(msg)), boost::bind(&handler::read_request, shared_from_this(), _1, _2));
   }

   void error(const char* msg, const char* error_type = "ERROR")
   {
      std::ostringstream oss;
      oss << error_type << ' ' << msg << "\r\n";
      buf_ = oss.str();

      boost::asio::async_write(
         socket_, boost::asio::buffer(buf_), boost::bind(&handler::hang_up, shared_from_this(), _1, _2));
   }

   void error(const char* location, const boost::system::error_code& e)
   {
      log::ERROR("handler<%1%>::%2%: %3%", shared_from_this(), location, e.message());
   }

   void error(const char* location, const boost::system::system_error& ex, bool echo = true)
   {
      log::ERROR("handler<%1%>::%2%: %3%", shared_from_this(), location, ex.code().message());

      if (echo)
      {
         buf_ = "SERVER_ERROR " + ex.code().message() + "\r\n";
         boost::asio::async_write(
            socket_, boost::asio::buffer(buf_), boost::bind(&handler::hang_up, shared_from_this(), _1, _2));      
      }
   }

   void hang_up(const boost::system::error_code& e, size_t bytes_transferred) {}

   const queue::size_type chunk_size_;

   socket_type socket_;
   request_parser& parser_;
   queue_map& queues_;
   stats& stats_;
   boost::asio::streambuf in_;
   std::string header_buf_;
   std::string buf_;
   request req_;

   iqstream pop_stream_;
   oqstream push_stream_;
};

} // darner

#endif // __DARNER_HANDLER_HPP__
