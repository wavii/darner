#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue.hpp"
#include "fixtures/basic_fs.hpp"

using namespace std;
using namespace boost::asio;
using namespace darner;

BOOST_AUTO_TEST_SUITE( fs_tests )

// test that we can write and then read a key
BOOST_FIXTURE_TEST_CASE( test_write_read, fixtures::basic_fs )
{
   string value = "I don’t ever watch dramas on a plane... I don’t be wanting to reflect";
   {
      fs::ostream os(*fs_, value.size(), 1);
      BOOST_REQUIRE_EQUAL(os.key(), 0);
      os.write(value, write_cb_);
      BOOST_REQUIRE(!write_error_);
   }
   fs::istream(*fs_, 0).read(read_cb_);
   BOOST_REQUIRE(!read_error_);
   BOOST_REQUIRE_EQUAL(read_value_, value);
}

// test that we can write multiple chunks to a key and then red them back
BOOST_FIXTURE_TEST_CASE( test_write_read_multiple, fixtures::basic_fs )
{
   string value1 = "I make awesome decisions in bike stores!!!";
   string value2 = "I always misspell genius SMH! The irony!";
   {
      fs::ostream os(*fs_, value1.size() + value2.size(), 2);
      BOOST_REQUIRE_EQUAL(os.key(), 0);
      os.write(value1, write_cb_);
      os.write(value2, write_cb_);
      BOOST_REQUIRE(!write_error_);
   }
   {
      fs::istream is(*fs_, 0);
      is.read(read_cb_);
      BOOST_REQUIRE(!read_error_);
      BOOST_REQUIRE_EQUAL(read_value_, value1);
      is.read(read_cb_);
      BOOST_REQUIRE(!read_error_);
      BOOST_REQUIRE_EQUAL(read_value_, value2);
      is.read(read_cb_);
      BOOST_REQUIRE_EQUAL(read_error_, boost::asio::error::eof);
   }
}

// test that we can write something, delete it, then no longer find it
BOOST_FIXTURE_TEST_CASE( test_delete, fixtures::basic_fs )
{
   string value = "I ordered the salmon medium instead of medium well I didn’t want to ruin the magic";
   fs::ostream(*fs_, value.size(), 1).write(value, write_cb_);;
   // now you see it...
   fs::istream(*fs_, 0).read(read_cb_);
   BOOST_REQUIRE(!read_error_);
   BOOST_REQUIRE_EQUAL(read_value_, value);
   // now you don't!
   fs::istream(*fs_, 0).remove(remove_cb_);
   fs::istream(*fs_, 0).read(read_cb_);
   BOOST_REQUIRE_EQUAL(read_error_, boost::asio::error::not_found);
}

// test that a size overrun returns an error
BOOST_FIXTURE_TEST_CASE( test_size_overrun, fixtures::basic_fs )
{
   string value = "Hotel robe got me feeling like a Sheik";
   fs::ostream(*fs_, value.size() - 1, 1).write(value, write_cb_);;
   BOOST_REQUIRE(write_error_);
}

BOOST_AUTO_TEST_SUITE_END()
