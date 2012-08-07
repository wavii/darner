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
   : queue_success_cb_(boost::bind(&basic_queue::success_cb, this, _1)),
     iqstream_success_cb_(boost::bind(&basic_queue::pop_cb, this, _1)),
     oqstream_success_cb_(boost::bind(&basic_queue::push_cb, this, _1)),
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
      darner::oqstream(*queue_, 1).write(value, oqstream_success_cb_);
   }

protected:

   void success_cb(const boost::system::error_code& error)
   {
      error_ = error;
   }

   void push_cb(const boost::system::error_code& error)
   {
      push_error_ = error;
   }

   void pop_cb(const boost::system::error_code& error)
   {
      pop_error_ = error;
   }

   boost::system::error_code error_;
   boost::system::error_code push_error_;
   boost::system::error_code pop_error_;
   std::string pop_value_;

   darner::queue::success_callback queue_success_cb_;
   darner::iqstream::success_callback iqstream_success_cb_;
   darner::oqstream::success_callback oqstream_success_cb_;

   boost::asio::io_service ios_;
   boost::shared_ptr<darner::queue> queue_;
   boost::filesystem::path tmp_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_QUEUE_HPP__