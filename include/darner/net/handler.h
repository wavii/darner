#ifndef __DARNER_HANDLER_HPP__
#define __DARNER_HANDLER_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/optional.hpp>
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
      queue::size_type chunk_size = 4096);

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

   // call done after a successful call or a failure. on fail, ensures the handler cleans up
   void done(bool success, const std::string& msg = "");

   void finalize(const boost::system::error_code& e, size_t bytes_transferred);

   const queue::size_type chunk_size_;

   socket_type socket_;
   request_parser& parser_;
   queue_map& queues_;
   stats& stats_;
   boost::asio::streambuf in_;
   std::string header_buf_;
   std::string buf_;
   request req_;

   boost::optional<iqstream> pop_stream_;
   boost::optional<oqstream> push_stream_;
};

} // darner

#endif // __DARNER_HANDLER_HPP__
