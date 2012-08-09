#ifndef __DARNER_STATS_HPP__
#define __DARNER_STATS_HPP__

#include <string>
#include <sstream>

#include <boost/cstdint.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

namespace darner {

// these stats are accumulated during the life of a process, but don't persist across processes
struct stats
{
   stats()
   : alive_since(boost::posix_time::microsec_clock::local_time()),
     items_enqueued(0),
     items_dequeued(0),
     conns_opened(0),
     conns_closed(0),
     gets(0),
     sets(0)
   {
   }

   void write(std::string& out)
   {
      boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
      boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
      std::ostringstream oss;
      oss << "STAT uptime " << (now - alive_since).total_seconds() << "\r\n";
      oss << "STAT time " << (now - epoch).total_seconds() << "\r\n";
      oss << "STAT version " << DARNER_VERSION << "\r\n";
      oss << "STAT curr_items " << items_dequeued - items_enqueued << "\r\n";
      oss << "STAT total_items " << items_enqueued << "\r\n";
      oss << "STAT curr_connections " << conns_opened - conns_closed << "\r\n";
      oss << "STAT total_connections " << conns_opened << "\r\n";
      oss << "STAT cmd_get " << gets << "\r\n";
      oss << "STAT cmd_set " << sets << "\r\n";
      oss << "END\r\n";
      out = oss.str();
   }

   boost::posix_time::ptime alive_since; // time we started up the server

   boost::uint64_t items_enqueued; // enqueued across all queues
   boost::uint64_t items_dequeued; // dequeued across all queues
   boost::uint64_t conns_opened;
   boost::uint64_t conns_closed;   // closed - opened gives you current # conns
   boost::uint64_t gets;           // counts even empty gets
   boost::uint64_t sets;

   boost::mutex mutex;
};

} // darner

#endif // __DARNER_STATS_HPP__
