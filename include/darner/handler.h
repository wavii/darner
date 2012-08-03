#ifndef __DARNER_HANDLER_H__
#define __DARNER_HANDLER_H__

#include <string>

#include <boost/cstdint.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

namespace darner {

/*
 * represents all the core application state for a darner server
 * 
 * connections pass themselves to the handler, which handles the request, then ultimately passes control back to the
 * connection via a common callback
 */
class request_handler
{
public:

   struct stats
   {
      stats()
      : alive_since(boost::posix_time::microsec_clock::local_time())
      {
      }

      boost::posix_time::ptime alive_since; // time we started up the server

      // these stats are accumulated during the life of a process, but don't persist across processes
      boost::uint64_t items_enqueued; // enqueued across all queues
      boost::uint64_t items_dequeued; // dequeued across all queues
      boost::uint64_t conns_opened;
      boost::uint64_t conns_closed;   // closed - opened gives you current # conns
      boost::uint64_t gets;           // counts even empty gets
      boost::uint64_t sets;

      boost::mutex mutex;
   };

   typedef boost::asio::ip::tcp::socket socket_type;
   typedef boost::function<void (const boost::system::error_code& error)> response_callback;

   request_handler(const std::string& data_path);

   void handle_stats(socket_type& socket, const response_callback& cb);

   stats& get_stats()
   {
      return stats_;
   }

private:

   std::string data_path_;
   stats stats_;
};

} // darner

#endif // __DARNER_HANDLER_H__
