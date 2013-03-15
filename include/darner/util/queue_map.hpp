#ifndef __DARNER_QUEUE_MAP_HPP__
#define __DARNER_QUEUE_MAP_HPP__

#include <string>
#include <map>

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/queue/queue.h"

namespace darner {

// maps a queue name to a queue instance, reloads queues
class queue_map
{
private:

   typedef std::map<std::string, boost::shared_ptr<queue> > container_type;

public:

   typedef container_type::iterator iterator;
   typedef container_type::const_iterator const_iterator;

   queue_map(boost::asio::io_service& ios, const std::string& data_path)
   : data_path_(data_path), ios_(ios)
   {
      boost::filesystem::directory_iterator end_it;
      for (boost::filesystem::directory_iterator it(data_path_); it != end_it; ++it)
      {
         std::string queue_name =
            boost::filesystem::path(it->path().filename()).string(); // useless recast for boost backwards compat
         boost::shared_ptr<queue> p(new queue(ios_, (data_path_ / queue_name).string()));
         queues_.insert(container_type::value_type(queue_name, p));
      }
   }

   boost::shared_ptr<queue> operator[] (const std::string& queue_name)
   {
      iterator it = queues_.find(queue_name);

      if (it == queues_.end())
      {
         boost::shared_ptr<queue> p(new queue(ios_, (data_path_ / queue_name).string()));
         it = queues_.insert(container_type::value_type(queue_name, p)).first;
      }

      return it->second;
   }

   void erase(const std::string& queue_name)
   {
      iterator it = queues_.find(queue_name);

      if (it == queues_.end())
         return;

      it->second->destroy();

      queues_.erase(it);
   }

   iterator begin()             { return queues_.begin(); }
   iterator end()               { return queues_.end(); }
   const_iterator begin() const { return queues_.begin(); }
   const_iterator end() const   { return queues_.end(); }

private:

   std::map<std::string, boost::shared_ptr<queue> > queues_;

   boost::filesystem::path data_path_;
   boost::asio::io_service& ios_;
};

} // darner

#endif // __DARNER_QUEUE_MAP_HPP__
