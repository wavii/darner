#ifndef __DARNER_STATS_HPP__
#define __DARNER_STATS_HPP__

#include <boost/thread/mutex.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace darner {

struct stats
{
   stats()
   : alive_since(boost::posix_time::microsec_clock::local_time())
   {
   }

   boost::posix_time::ptime alive_since; // time we started up the server
   boost::uint64_t curr_items;           // items currently enqueued
   boost::uint64_t curr_connections;     // idle or active open connections

   // TODO: add more stats, persist them after shutdown

   boost::mutex mutex;
};

} // darner

#endif // __DARNER_STATS_HPP__
