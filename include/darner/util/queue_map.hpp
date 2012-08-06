#ifndef __DARNER_QUEUE_MAP_HPP__
#define __DARNER_QUEUE_MAP_HPP__

#include <string>

#include <boost/asio.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/queue/queue.h"

namespace darner {

// maps a queue name to a queue instance, reloads queues
class queue_map
{
public:

   typedef boost::ptr_map<std::string, queue>::iterator iterator;
   typedef boost::ptr_map<std::string, queue>::const_iterator const_iterator;

   queue_map(boost::asio::io_service& ios, const std::string& data_path)
   : data_path_(data_path), ios_(ios)
   {
      boost::filesystem::directory_iterator end_it;
      for (boost::filesystem::directory_iterator it(data_path_); it != end_it; ++it)
      {
         std::string queue_name = it->path().leaf().string();
         queues_.insert(queue_name, new queue(ios_, it->path().string()));
      }
   }

   queue& operator[] (const std::string& queue_name)
   {
      boost::mutex::scoped_lock lock(mutex);
      boost::ptr_map<std::string, queue>::iterator it = queues_.find(queue_name);
      if (it == queues_.end())
      {
         std::string q(queue_name); // some strange ptr_map limitation, needs non-const key
         it = queues_.insert(q, new queue(ios_, (data_path_ / queue_name).string())).first;
      }
      return *it->second;
   }

private:

   boost::ptr_map<std::string, queue> queues_;

   boost::filesystem::path data_path_;
   boost::asio::io_service& ios_;

   boost::mutex mutex;
};

} // darner

#endif // __DARNER_QUEUE_MAP_HPP__
