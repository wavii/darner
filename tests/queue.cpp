#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue.hpp"
#include "fixtures/basic_queue.hpp"

using namespace std;
using namespace boost::asio;

BOOST_AUTO_TEST_SUITE( queue_tests )

// test that we can push and pop
BOOST_FIXTURE_TEST_CASE( test_push_pop, fixtures::basic_queue )
{
   string value = "I hate when I'm on a flight and I wake up with a water bottle next to me like oh great now I gotta "
                  "be responsible for this water bottle";
   queue_->push(value, push_cb_);
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_key_, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// test having nothing to pop
BOOST_FIXTURE_TEST_CASE( test_pop_empty, fixtures::basic_queue )
{
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(pop_error_);
}

// test a pop wait
BOOST_FIXTURE_TEST_CASE( test_pop_wait, fixtures::basic_queue )
{
   queue_->pop(100, pop_cb_);
   deadline_timer timer(ios_, boost::posix_time::milliseconds(10));
   string value = "sometimes I push the door close button on people running towards the elevator. I just need my own "
                  "elevator sometimes, my 7 floor sanctuary";
   timer.async_wait(boost::bind(&fixtures::basic_queue::delayed_push, this, boost::cref(value), _1));
   ios_.run();
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_key_, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// now the opposite, test a pop wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_timeout, fixtures::basic_queue )
{
   queue_->pop(10, pop_cb_);
   deadline_timer timer(ios_, boost::posix_time::milliseconds(50));
   string value = "Classical music is tight yo";
   timer.async_wait(boost::bind(&fixtures::basic_queue::delayed_push, this, boost::cref(value), _1));
   ios_.run();
   BOOST_REQUIRE(pop_error_);
}

// test that we can close and reopen a queue
BOOST_FIXTURE_TEST_CASE( test_queue_close_reopen, fixtures::basic_queue )
{
   string value = "Do you know where to find marble conference tables? I’m looking to have a conference…not until I "
                  "get the table though";
   queue_->push(value, push_cb_);
   queue_.reset(new darner::queue(ios_, (tmp_ / "queue").string()));
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_key_, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// test we report count correctly
BOOST_FIXTURE_TEST_CASE( test_queue_count, fixtures::basic_queue )
{
   string value = "NO ALCOHOL BEFORE TATTOOS";
   queue_->push(value, push_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
   // even beginning a pop lowers the count...
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0);
   // but returning it raises it back up
   queue_->pop_end(0, false, pop_end_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
