#ifndef __DARNER_HANDLER_HPP__
#define __DARNER_HANDLER_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "darner/util/log.h"
#include "darner/net/request.h"
#include "darner/util/stats.hpp"

namespace darner {

class handler : public boost::enable_shared_from_this<handler>
{
public:

   typedef boost::asio::ip::tcp::socket socket_type;
   typedef boost::shared_ptr<handler> ptr_type;

   handler(boost::asio::io_service& ios, request_parser& parser, stats& _stats, size_t max_frame_size = 4096);

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

   // empty bail-out function, usually at the end of an error
   void do_nothing(const boost::system::error_code& e, size_t bytes_transferred);

   // all the ops:

   void write_stats();

   void write_version();

   void flush();

   void flush_all();

   void set();

   void get();

   socket_type socket_;
   request_parser& parser_;
   stats& stats_;
   boost::asio::streambuf in_buf_;
   std::string out_buf_;
   request req_;
};

} // darner

#endif // __DARNER_HANDLER_HPP__
