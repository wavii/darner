#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue/queue.h"
#include "darner/queue/iqstream.h"
#include "darner/queue/oqstream.h"
#include "fixtures/basic_queue.hpp"

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace darner;

BOOST_AUTO_TEST_SUITE( queue_tests )

// test that we can push and pop
BOOST_FIXTURE_TEST_CASE( test_push_pop, fixtures::basic_queue )
{
   string value = "I hate when I'm on a flight and I wake up with a water bottle next to me like oh great now I gotta "
                  "be responsible for this water bottle";
   oqs_.open(queue_, 1);
   oqs_.write(value);

   BOOST_REQUIRE(iqs_.open(queue_));

   iqs_.read(pop_value_);
   BOOST_REQUIRE_EQUAL(value, pop_value_);
}

// test having nothing to pop
BOOST_FIXTURE_TEST_CASE( test_pop_empty, fixtures::basic_queue )
{
   BOOST_REQUIRE(!iqs_.open(queue_));
}

// test a pop wait
BOOST_FIXTURE_TEST_CASE( test_pop_wait, fixtures::basic_queue )
{
   string value = "sometimes I push the door close button on people running towards the elevator. I just need my own "
                  "elevator sometimes, my 7 floor sanctuary";
   posix_time::ptime beg = boost::posix_time::microsec_clock::local_time();
   deadline_timer timer(ios_, posix_time::milliseconds(10));
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push, this, boost::ref(value), _1));
   queue_->wait(100, wait_cb_);
   ios_.run();
   boost::posix_time::ptime end = boost::posix_time::microsec_clock::local_time();

   BOOST_REQUIRE(!error_);
   BOOST_REQUIRE_LT((end - beg).total_milliseconds(), 50); // should come back sooner than the timeout
}

// test multiple waiters
BOOST_FIXTURE_TEST_CASE( test_multiple_pop_wait, fixtures::basic_queue )
{
   string value1 = "I’m just tryna keep it symmetrical";
   string value2 = "Hotel robe got me feeling like a Sheik";
   posix_time::ptime beg = boost::posix_time::microsec_clock::local_time();
   deadline_timer timer1(ios_, posix_time::milliseconds(10));
   timer1.async_wait(bind(&fixtures::basic_queue::delayed_push, this, boost::ref(value1), _1));
   deadline_timer timer2(ios_, posix_time::milliseconds(20));
   timer2.async_wait(bind(&fixtures::basic_queue::delayed_push, this, boost::ref(value2), _1));
   queue_->wait(100, wait_cb_);
   queue_->wait(100, wait_cb_);
   ios_.run();
   boost::posix_time::ptime end = boost::posix_time::microsec_clock::local_time();

   BOOST_REQUIRE(!error_);
   BOOST_REQUIRE_EQUAL(cb_count_, 2);
   BOOST_REQUIRE_LT((end - beg).total_milliseconds(), 50); // should come back sooner than the timeout
}

// test the race condition where a wait callback does work that crosses it over the wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_race, fixtures::basic_queue )
{
   string value = "Fur pillows are hard to actually sleep on";
   deadline_timer timer(ios_, posix_time::milliseconds(80));
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push_block, this, boost::ref(value), _1));
   queue_->wait(100, wait_cb_);
   ios_.run();

   BOOST_REQUIRE(!error_);
}

// now the opposite, test a pop wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_timeout, fixtures::basic_queue )
{
   string value = "Classical music is tight yo";
   deadline_timer timer(ios_, posix_time::milliseconds(50));
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push, this, boost::ref(value), _1));
   queue_->wait(10, wait_cb_);
   ios_.run();

   BOOST_REQUIRE(error_);
}

// test that we can close and reopen a queue
BOOST_FIXTURE_TEST_CASE( test_queue_close_reopen, fixtures::basic_queue )
{
   string value = "Do you know where to find marble conference tables? I’m looking to have a conference…not until I "
                  "get the table though";

   oqs_.open(queue_, 1);
   oqs_.write(value);
   queue_.reset(new darner::queue(ios_, (tmp_ / "queue").string()));
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);

   iqs_.open(queue_);
   iqs_.read(pop_value_);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// test we report count correctly
BOOST_FIXTURE_TEST_CASE( test_queue_count, fixtures::basic_queue )
{
   string value = "NO ALCOHOL BEFORE TATTOOS";

   oqs_.open(queue_, 1);
   oqs_.write(value);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);

   // even beginning a pop lowers the count...
   iqs_.open(queue_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0);

   // but returning it raises it back up
   iqs_.close(false);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
}

// test overruning an oqstream raises 
BOOST_FIXTURE_TEST_CASE( test_oqstream_overflow, fixtures::basic_queue )
{
   string value = "I would like to thank Julius Caesar for originating my hairstyle";

   oqs_.open(queue_, 1);
   oqs_.write(value);
   BOOST_REQUIRE_THROW(oqs_.write(value), system::system_error);
}

// test that we can push/pop multi-chunked
BOOST_FIXTURE_TEST_CASE( test_multi_chunked, fixtures::basic_queue )
{
   string value1 = "I don’t ever watch dramas on a plane... I don’t be wanting to reflect";
   string value2 = "I make awesome decisions in bike stores!!!";
   oqs_.open(queue_, 2);
   oqs_.write(value1);

   BOOST_REQUIRE_EQUAL(queue_->count(), 0); // not ready yet...
   BOOST_REQUIRE_EQUAL(oqs_.tell(), value1.size());

   oqs_.write(value2);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1); // okay it's done
   BOOST_REQUIRE_EQUAL(oqs_.tell(), value1.size() + value2.size());

   iqs_.open(queue_);
   iqs_.read(pop_value_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0); // even beginning a pop lowers the count...
   BOOST_REQUIRE_EQUAL(iqs_.tell(), value1.size());
   BOOST_REQUIRE_EQUAL(iqs_.size(), value1.size() + value2.size());
   BOOST_REQUIRE_EQUAL(pop_value_, value1);

   iqs_.read(pop_value_);
   BOOST_REQUIRE_EQUAL(iqs_.tell(), value1.size() + value2.size());
   BOOST_REQUIRE_EQUAL(pop_value_, value2);

   // finally, the delete
   iqs_.close(true);

   // should be nothing left to pop
   BOOST_REQUIRE(!iqs_.open(queue_));
}

// test that we can cancel a multi-chunked push
BOOST_FIXTURE_TEST_CASE( test_push_cancel, fixtures::basic_queue )
{
   string value = "I ordered the salmon medium instead of medium well I didn’t want to ruin the magic";
   oqs_.open(queue_, 2);
   oqs_.write(value);

   BOOST_REQUIRE_EQUAL(oqs_.tell(), value.size());

   oqs_.cancel();

   // shouldn't be able to finish the push now
   BOOST_REQUIRE_THROW(oqs_.write(value), system::system_error);
}

// test that we can push a value that ends in a null-zero
BOOST_FIXTURE_TEST_CASE( test_push_zero, fixtures::basic_queue )
{
   string value = string("I'm sorry Taylor.") + '\0';
   oqs_.open(queue_, 1);
   oqs_.write(value);
   iqs_.open(queue_);
   iqs_.read(pop_value_);

   BOOST_REQUIRE_EQUAL(value, pop_value_);
}

// test that we can delete a queue when we are done with it
BOOST_FIXTURE_TEST_CASE( test_delete_queue, fixtures::basic_queue )
{
   queue_->destroy();
   BOOST_REQUIRE(!filesystem::exists(tmp_ / "queue"));
   BOOST_REQUIRE(filesystem::exists(tmp_ / "queue.0")); // first delete gets .0
   darner::queue queue2(ios_, (tmp_ / "queue").string());
   queue2.destroy();
   BOOST_REQUIRE(filesystem::exists(tmp_ / "queue.1")); // second delete gets .1
   queue_.reset();
   BOOST_REQUIRE(!filesystem::exists(tmp_ / "queue.0")); // finally, destroying the queue deletes the journal
}

BOOST_AUTO_TEST_SUITE_END()
