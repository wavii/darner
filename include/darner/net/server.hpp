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
          unsigned short listen_port,
          size_t num_workers)
   : listen_port_(listen_port),
     num_workers_(num_workers),
     disk_work_(new boost::asio::io_service::work(disk_ios_)),
     acceptor_(net_ios_),
     strand_(net_ios_),
     queues_(disk_ios_, data_path)
   {
      // open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
      boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), listen_port_);
      acceptor_.open(endpoint.protocol());
      acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
      acceptor_.bind(endpoint);
      acceptor_.listen();

      // get our first conn ready
      handler_ = handler::ptr_type(new handler(net_ios_, parser_, queues_, stats_));

      // pump the first async accept into the loop
      acceptor_.async_accept(handler_->socket(),
         boost::bind(&server::handle_accept, this, boost::asio::placeholders::error));

      // start up the disk service
      disk_worker_ = boost::thread(boost::bind(&boost::asio::io_service::run, &disk_ios_));

      // spin up the threads that run the handlers
      for (size_t i = 0; i != num_workers_; ++i)
         net_workers_.create_thread(boost::bind(&boost::asio::io_service::run, &net_ios_));
   }

   void stop()
   {
      strand_.post(boost::bind(&server::handle_close, this));
      // TODO: also close any idling clients
      net_workers_.join_all();

      // let the disk thread exit
      disk_work_.reset();
      disk_worker_.join();
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

      handler_ = handler::ptr_type(new handler(net_ios_, parser_, queues_, stats_));
      acceptor_.async_accept(handler_->socket(),
         strand_.wrap(
            boost::bind(&server::handle_accept, this, boost::asio::placeholders::error)));
   }

   void handle_close()
   {
      acceptor_.close();
   }

   unsigned short listen_port_;
   size_t num_workers_;

   // net_ios_ runs all the handlers and the acceptor, disk_ios_ is exclusively for journal/disk i/o
   // net_ios_ is safe for multiple threads
   boost::asio::io_service net_ios_;
   boost::asio::io_service disk_ios_;
   boost::scoped_ptr<boost::asio::io_service::work> disk_work_;
   boost::asio::ip::tcp::acceptor acceptor_;
   boost::asio::strand strand_; // to avoid close() and async_accept firing at the same time
   queue_map queues_;
   request_parser parser_;
   stats stats_;
   handler::ptr_type handler_;
   boost::thread_group net_workers_;
   boost::thread disk_worker_;
};

} // darner

#endif // __DARNER_SERVER_HPP__
