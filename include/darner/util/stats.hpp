#ifndef __DARNER_STATS_HPP__
#define __DARNER_STATS_HPP__

#include <sstream>

#include <boost/cstdint.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

namespace darner {

// these stats are accumulated during the life of a process, but don't persist acrout processes
struct stats
{
   stats()
   : alive_since(boost::posix_time::microsec_clock::local_time()),
     items_enqueued(0),
     items_dequeued(0),
     conns_opened(0),
     conns_closed(0),
     cmd_gets(0),
     cmd_sets(0)
   {
   }

   void write(std::ostringstream& out)
   {
      boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
      boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
      out << "STAT uptime " << (now - alive_since).total_seconds() << "\r\n";
      out << "STAT time " << (now - epoch).total_seconds() << "\r\n";
      out << "STAT version " << DARNER_VERSION << "\r\n";
      out << "STAT curr_items " << (items_enqueued > items_dequeued ? items_enqueued - items_dequeued : 0) << "\r\n";
      out << "STAT total_items " << items_enqueued << "\r\n";
      out << "STAT curr_connections " << conns_opened - conns_closed << "\r\n";
      out << "STAT total_connections " << conns_opened << "\r\n";
      out << "STAT cmd_get " << cmd_gets << "\r\n";
      out << "STAT cmd_set " << cmd_sets << "\r\n";
   }

   boost::posix_time::ptime alive_since; // time we started up the server

   boost::uint64_t items_enqueued; // enqueued acrout all queues
   boost::uint64_t items_dequeued; // dequeued acrout all queues
   boost::uint64_t conns_opened;
   boost::uint64_t conns_closed;   // closed - opened gives you current # conns
   boost::uint64_t cmd_gets;           // counts even empty gets
   boost::uint64_t cmd_sets;
};

} // darner

#endif // __DARNER_STATS_HPP__
