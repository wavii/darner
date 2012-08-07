#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue/queue.h"
#include "darner/queue/istream.h"
#include "darner/queue/ostream.h"
#include "fixtures/basic_queue.hpp"

using namespace std;
using namespace boost;
using namespace boost::asio;

BOOST_AUTO_TEST_SUITE( queue_tests )

// test that we can push and pop
BOOST_FIXTURE_TEST_CASE( test_push_pop, fixtures::basic_queue )
{
   string value = "I hate when I'm on a flight and I wake up with a water bottle next to me like oh great now I gotta "
                  "be responsible for this water bottle";
   darner::ostream(*queue_, 1).write(value, ostream_success_cb_);
   BOOST_REQUIRE(!push_error_);
   darner::istream(*queue_, 0).read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(value, pop_value_);
}

// test having nothing to pop
BOOST_FIXTURE_TEST_CASE( test_pop_empty, fixtures::basic_queue )
{
   darner::istream(*queue_, 0).read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE(pop_error_);
}

// test a pop wait
BOOST_FIXTURE_TEST_CASE( test_pop_wait, fixtures::basic_queue )
{
   darner::istream is(*queue_, 100);
   is.read(pop_value_, istream_success_cb_);
   deadline_timer timer(ios_, posix_time::milliseconds(10));
   string value = "sometimes I push the door close button on people running towards the elevator. I just need my own "
                  "elevator sometimes, my 7 floor sanctuary";
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push, this, ref(value), _1));
   ios_.run();
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// now the opposite, test a pop wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_timeout, fixtures::basic_queue )
{
   darner::istream is(*queue_, 10);
   is.read(pop_value_, istream_success_cb_);
   deadline_timer timer(ios_, posix_time::milliseconds(50));
   string value = "Classical music is tight yo";
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push, this, ref(value), _1));
   ios_.run();
   BOOST_REQUIRE(pop_error_);
}

// test that we can close and reopen a queue
BOOST_FIXTURE_TEST_CASE( test_queue_close_reopen, fixtures::basic_queue )
{
   string value = "Do you know where to find marble conference tables? I’m looking to have a conference…not until I "
                  "get the table though";
   darner::ostream(*queue_, 1).write(value, ostream_success_cb_);
   queue_.reset(new darner::queue(ios_, (tmp_ / "queue").string()));
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
   darner::istream(*queue_, 0).read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// test we report count correctly
BOOST_FIXTURE_TEST_CASE( test_queue_count, fixtures::basic_queue )
{
   string value = "NO ALCOHOL BEFORE TATTOOS";
   darner::ostream(*queue_, 1).write(value, ostream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
   // even beginning a pop lowers the count...
   darner::istream is(*queue_, 0);
   is.read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0);
   // but returning it raises it back up
   is.close(false, istream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
}

// test that we can push/pop multi-chunked
BOOST_FIXTURE_TEST_CASE( test_multi_chunked, fixtures::basic_queue )
{
   string value1 = "I don’t ever watch dramas on a plane... I don’t be wanting to reflect";
   string value2 = "I make awesome decisions in bike stores!!!";
   darner::ostream os(*queue_, 2);
   os.write(value1, ostream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0); // not ready yet...
   BOOST_REQUIRE_EQUAL(os.tell(), value1.size());

   os.write(value2, ostream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1); // okay it's done
   BOOST_REQUIRE_EQUAL(os.tell(), value1.size() + value2.size());

   // even beginning a pop lowers the count...
   darner::istream is(*queue_, 0);
   is.read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0);
   BOOST_REQUIRE_EQUAL(is.tell(), value1.size());
   BOOST_REQUIRE_EQUAL(is.size(), value1.size() + value2.size());
   BOOST_REQUIRE_EQUAL(pop_value_, value1);

   is.read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE_EQUAL(is.tell(), value1.size() + value2.size());
   BOOST_REQUIRE_EQUAL(pop_value_, value2);

   // finally, the delete
   is.close(true, istream_success_cb_);

   // should be nothing left to pop
   darner::istream(*queue_, 0).read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE(pop_error_); // not_found!
}

// test that we can cancel a multi-chunked push
BOOST_FIXTURE_TEST_CASE( test_push_cancel, fixtures::basic_queue )
{
   string value = "I ordered the salmon medium instead of medium well I didn’t want to ruin the magic";
   darner::ostream os(*queue_, 2);
   os.write(value, ostream_success_cb_);
   BOOST_REQUIRE_EQUAL(os.tell(), value.size());

   os.cancel(ostream_success_cb_);
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE_EQUAL(os.tell(), 0);
}

// test that we can push a value that ends in a null-zero
BOOST_FIXTURE_TEST_CASE( test_push_zero, fixtures::basic_queue )
{
   string value = string("I'm sorry Taylor.") + '\0';
   darner::ostream(*queue_, 1).write(value, ostream_success_cb_);
   BOOST_REQUIRE(!push_error_);
   darner::istream(*queue_, 0).read(pop_value_, istream_success_cb_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(value, pop_value_);
}

BOOST_AUTO_TEST_SUITE_END()
