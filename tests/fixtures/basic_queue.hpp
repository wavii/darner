#ifndef __TESTS_FIXTURES_BASIC_QUEUE_HPP__
#define __TESTS_FIXTURES_BASIC_QUEUE_HPP__

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/queue/queue.h"

namespace fixtures {

// create a single basic queue and destroy/delete it when done
class basic_queue
{
public:

   basic_queue()
   : push_cb_(boost::bind(&basic_queue::push_cb, this, _1, _2)),
     pop_cb_(boost::bind(&basic_queue::pop_cb, this, _1, _2, _3)),
     pop_end_cb_(boost::bind(&basic_queue::pop_end_cb, this, _1)),
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
      queue_->push(value, push_cb_);
   }

protected:

   void push_cb(const boost::system::error_code& error, const darner::file_type& file)
   {
      push_error_ = error;
      push_file_ = file;
   }

   void pop_cb(const boost::system::error_code& error, const darner::file_type& file, const std::string& value)
   {
      pop_error_ = error;
      pop_file_ = file;
      pop_value_ = value;
   }

   void pop_end_cb(const boost::system::error_code& error)
   {
      pop_end_error_ = error;
   }

   boost::system::error_code push_error_;
   darner::file_type push_file_;
   boost::system::error_code pop_error_;
   darner::file_type pop_file_;
   std::string pop_value_;

   boost::system::error_code pop_end_error_;

   darner::queue::push_callback push_cb_;
   darner::queue::pop_callback pop_cb_;
   darner::queue::success_callback pop_end_cb_;
   boost::asio::io_service ios_;
   boost::shared_ptr<darner::queue> queue_;
   boost::filesystem::path tmp_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_QUEUE_HPP__