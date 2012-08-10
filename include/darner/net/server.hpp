#ifndef __DARNER_SERVER_HPP__
#define __DARNER_SERVER_HPP__

#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "darner/util/log.h"
#include "darner/util/stats.hpp"
#include "darner/util/queue_map.hpp"
#include "darner/net/request.h"
#include "darner/net/handler.h"

namespace darner {

/*
 * server loads up the queues, handles accepts and spawns off clients for each new accept
 */
class server
{
public:

   server(const std::string& data_path,
          unsigned short listen_port)
   : listen_port_(listen_port),
     acceptor_(ios_),
     queues_(ios_, data_path)
   {
      // open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
      boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), listen_port_);
      acceptor_.open(endpoint.protocol());
      acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
      acceptor_.bind(endpoint);
      acceptor_.listen();

      // get our first conn ready
      handler_ = handler::ptr_type(new handler(ios_, parser_, queues_, stats_));

      // pump the first async accept into the loop
      acceptor_.async_accept(handler_->socket(),
         boost::bind(&server::handle_accept, this, boost::asio::placeholders::error));

      // start up the event loop for the service
      runner_ = boost::thread(boost::bind(&boost::asio::io_service::run, &ios_));
   }

   void stop()
   {
      ios_.post(boost::bind(&server::handle_close, this));

      // let the thread exit
      runner_.join();
   }

private:

   void handle_accept(const boost::system::error_code& e)
   {
      if (e)
      {
         if (e != boost::asio::error::operation_aborted) // abort comes from canceling the acceptor
            log::ERROR("server::handle_accept: %1%", e.message());
         return;
      }

      handler_->start();

      handler_ = handler::ptr_type(new handler(ios_, parser_, queues_, stats_));
      acceptor_.async_accept(handler_->socket(),
         boost::bind(&server::handle_accept, this, boost::asio::placeholders::error));
   }

   void handle_close()
   {
      acceptor_.close();
   }

   unsigned short listen_port_;

   boost::asio::io_service ios_;

   boost::scoped_ptr<boost::asio::io_service::work> work_;
   boost::asio::ip::tcp::acceptor acceptor_;

   queue_map queues_;
   request_parser parser_;
   stats stats_;
   handler::ptr_type handler_;
   boost::thread runner_;
};

} // darner

#endif // __DARNER_SERVER_HPP__
