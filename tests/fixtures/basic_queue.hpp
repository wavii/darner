#ifndef __TESTS_FIXTURES_BASIC_QUEUE_HPP__
#define __TESTS_FIXTURES_BASIC_QUEUE_HPP__

#include <boost/thread.hpp>
#include <boost/version.hpp>
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
   : cb_count_(0),
     wait_cb_(boost::bind(&basic_queue::wait_cb, this, _1)),
#if BOOST_VERSION < 104600
     tmp_("/tmp/basic_queue_fixture")
#else
     tmp_(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
#endif
   {
      boost::filesystem::create_directory(tmp_);
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

   void delayed_push_block(std::string& value, const boost::system::error_code& error)
   {
      // just like above, but blocks for a short while
      boost::this_thread::sleep(boost::posix_time::milliseconds(30));
      darner::oqstream oqs;
      oqs.open(queue_, 1);
      oqs.write(value);
   }

protected:

   void wait_cb(const boost::system::error_code& error)
   {
      if (!error_)
         error_ = error;
      ++cb_count_;
   }

   std::string pop_value_;

   boost::system::error_code error_;
   size_t cb_count_;
   darner::queue::wait_callback wait_cb_;

   boost::asio::io_service ios_;
   boost::shared_ptr<darner::queue> queue_;
   darner::oqstream oqs_;
   darner::iqstream iqs_;
   boost::filesystem::path tmp_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_QUEUE_HPP__