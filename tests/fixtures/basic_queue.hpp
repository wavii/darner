#ifndef __TESTS_FIXTURES_BASIC_QUEUE_HPP__
#define __TESTS_FIXTURES_BASIC_QUEUE_HPP__

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/queue/queue.h"
#include "darner/queue/iqstream.h"
#include "darner/queue/oqstream.h"

namespace fixtures {

// create a single basic queue and destroy/delete it when done
class basic_queue
{
public:

   basic_queue()
   : wait_cb_(boost::bind(&basic_queue::wait_cb, this, _1)),
     tmp_(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
   {
      boost::filesystem::create_directories(tmp_);
      queue_.reset(new darner::queue(ios_, (tmp_ / "queue").string()));
   }

   virtual ~basic_queue()
   {
      queue_.reset();
      boost::filesystem::remove_all(tmp_);
   }

   void delayed_push(std::string& value, const boost::system::error_code& error)
   {
      darner::oqstream oqs;
      oqs.open(queue_, 1);
      oqs.write(value);
   }

protected:

   void wait_cb(const boost::system::error_code& error)
   {
      error_ = error;
   }

   std::string pop_value_;

   boost::system::error_code error_;
   darner::queue::wait_callback wait_cb_;

   boost::asio::io_service ios_;
   boost::shared_ptr<darner::queue> queue_;
   darner::oqstream oqs_;
   darner::iqstream iqs_;
   boost::filesystem::path tmp_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_QUEUE_HPP__