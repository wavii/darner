#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include <string>

#include "darner/net/request.h"
#include "fixtures/basic_request.hpp"

using namespace std;

BOOST_AUTO_TEST_SUITE( request_tests )

// test we read stats correctly
BOOST_FIXTURE_TEST_CASE( test_stats, fixtures::basic_request )
{
   BOOST_REQUIRE(parser_.parse(request_, string("STATS\r\n")));
   BOOST_REQUIRE_EQUAL(request_.type, darner::request::RT_STATS);
}

// test that we get the queue name for a flush command correctly
BOOST_FIXTURE_TEST_CASE( test_flush, fixtures::basic_request )
{
   BOOST_REQUIRE(parser_.parse(request_, string("FLUSH foo+meow\r\n")));
   BOOST_REQUIRE_EQUAL(request_.type, darner::request::RT_FLUSH);
   BOOST_REQUIRE_EQUAL(request_.queue, "foo+meow");
}

// test that we get the num bytes for a set correctly
BOOST_FIXTURE_TEST_CASE( test_set, fixtures::basic_request )
{
   BOOST_REQUIRE(parser_.parse(request_, string("SET foo+meow 0 0 31337\r\n")));
   BOOST_REQUIRE_EQUAL(request_.type, darner::request::RT_SET);
   BOOST_REQUIRE_EQUAL(request_.queue, "foo+meow");
   BOOST_REQUIRE_EQUAL(request_.num_bytes, 31337);
}

// test that we get some options correctly for a get
BOOST_FIXTURE_TEST_CASE( test_get, fixtures::basic_request )
{
   BOOST_REQUIRE(parser_.parse(request_, string("GET foo+meow/t=500/close/open\r\n")));
   BOOST_REQUIRE_EQUAL(request_.type, darner::request::RT_GET);
   BOOST_REQUIRE_EQUAL(request_.queue, "foo+meow");
   BOOST_REQUIRE(request_.get_open);
   BOOST_REQUIRE(request_.get_close);
   BOOST_REQUIRE(!request_.get_abort);
   BOOST_REQUIRE_EQUAL(request_.wait_ms, 500);
}

// test that reparsing clears fields that were previously set
BOOST_FIXTURE_TEST_CASE( test_reparse, fixtures::basic_request )
{
   BOOST_REQUIRE(parser_.parse(request_, string("GET foo+meow/t=500/close/open\r\n")));
   BOOST_REQUIRE_EQUAL(request_.wait_ms, 500);
   BOOST_REQUIRE(parser_.parse(request_, string("STATS\r\n")));
   BOOST_REQUIRE_EQUAL(request_.wait_ms, 0);
}

BOOST_AUTO_TEST_SUITE_END()
