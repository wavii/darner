#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue.hpp"
#include "fixtures/basic.hpp"

using namespace std;
using namespace boost::asio;

BOOST_AUTO_TEST_SUITE( queue_tests )

// test that we can push and pop
BOOST_FIXTURE_TEST_CASE( test_push_pop, fixtures::basic )
{
   string value = "I hate when I'm on a flight and I wake up with a water bottle next to me like oh great now I gotta "
                  "be responsible for this water bottle";
   queue_->push(value, push_cb_);
   queue_->pop(0, pop_cb_);
   ios_.run();
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_key_, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// test having nothing to pop
BOOST_FIXTURE_TEST_CASE( test_pop_empty, fixtures::basic )
{
   queue_->pop(0, pop_cb_);
   ios_.run();
   BOOST_REQUIRE(pop_error_);
}

// test a pop wait
BOOST_FIXTURE_TEST_CASE( test_pop_wait, fixtures::basic )
{
   queue_->pop(100, pop_cb_);
   deadline_timer timer(ios_, boost::posix_time::milliseconds(10));
   string value = "sometimes I push the door close button on people running towards the elevator. I just need my own "
                  "elevator sometimes, my 7 floor sanctuary";
   timer.async_wait(boost::bind(&fixtures::basic::delayed_push, this, boost::cref(value), _1));
   ios_.run();
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_key_, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// now the opposite, test a pop wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_timeout, fixtures::basic )
{
   queue_->pop(10, pop_cb_);
   deadline_timer timer(ios_, boost::posix_time::milliseconds(50));
   string value = "Classical music is tight yo";
   timer.async_wait(boost::bind(&fixtures::basic::delayed_push, this, boost::cref(value), _1));
   ios_.run();
   BOOST_REQUIRE(pop_error_);
}

BOOST_AUTO_TEST_SUITE_END()
