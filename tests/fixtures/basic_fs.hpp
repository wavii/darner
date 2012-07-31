#ifndef __TESTS_FIXTURES_BASIC_FS_HPP__
#define __TESTS_FIXTURES_BASIC_FS_HPP__

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/fs.hpp"

namespace fixtures {

// create a single basic fs
class basic_fs
{
public:

   basic_fs()
   : read_cb_(boost::bind(&basic_fs::read_cb, this, _1, _2)),
     remove_cb_(boost::bind(&basic_fs::remove_cb, this, _1)),
     write_cb_(boost::bind(&basic_fs::write_cb, this, _1)),
     tmp_(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
   {
      boost::filesystem::create_directories(tmp_);
      fs_.reset(new darner::fs(ios_, (tmp_ / "fs").string()));
   }

   virtual ~basic_fs()
   {
      fs_.reset();
      boost::filesystem::remove_all(tmp_);
   }

protected:

   void read_cb(const boost::system::error_code& error, const std::string& value)
   {
      read_error_ = error;
      read_value_ = value;
   }

   void remove_cb(const boost::system::error_code& error)
   {
      remove_error_ = error;
   }

   void write_cb(const boost::system::error_code& error)
   {
      write_error_ = error;
   }

   boost::system::error_code remove_error_;
   boost::system::error_code read_error_;
   std::string read_value_;
   boost::system::error_code write_error_;

   darner::fs::istream::read_callback read_cb_;
   darner::fs::istream::remove_callback remove_cb_;
   darner::fs::ostream::write_callback write_cb_;
   boost::asio::io_service ios_;
   boost::shared_ptr<darner::fs> fs_;
   boost::filesystem::path tmp_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_FS_HPP__
