#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue/queue.h"
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
   queue_->push(value, push_cb_);
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_file_.id, 0);
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
   deadline_timer timer(ios_, posix_time::milliseconds(10));
   string value = "sometimes I push the door close button on people running towards the elevator. I just need my own "
                  "elevator sometimes, my 7 floor sanctuary";
   timer.async_wait(bind(&fixtures::basic_queue::delayed_push, this, ref(value), _1));
   ios_.run();
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_file_.id, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

// now the opposite, test a pop wait timeout
BOOST_FIXTURE_TEST_CASE( test_pop_wait_timeout, fixtures::basic_queue )
{
   queue_->pop(10, pop_cb_);
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
   queue_->push(value, push_cb_);
   queue_.reset(new darner::queue(ios_, (tmp_ / "queue").string()));
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_file_.id, 0);
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
   queue_->pop_end(pop_file_, false, pop_end_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1);
}

// test that we can push/pop multi-chunked
BOOST_FIXTURE_TEST_CASE( test_multi_chunked, fixtures::basic_queue )
{
   string value1 = "I don’t ever watch dramas on a plane... I don’t be wanting to reflect";
   string value2 = "I make awesome decisions in bike stores!!!";
   queue_->push_reserve(push_file_, 2);
   BOOST_REQUIRE_EQUAL(push_file_.header.chunk_beg, 0);
   BOOST_REQUIRE_EQUAL(push_file_.header.chunk_end, 2);
   BOOST_REQUIRE_EQUAL(push_file_.tell, 0);

   queue_->push_chunk(push_file_, value1, push_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0); // not ready yet...
   BOOST_REQUIRE_EQUAL(push_file_.tell, 1);

   queue_->push_chunk(push_file_, value2, push_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 1); // okay it's done
   BOOST_REQUIRE_EQUAL(push_file_.tell, 2);

   // even beginning a pop lowers the count...
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE_EQUAL(queue_->count(), 0);
   BOOST_REQUIRE_EQUAL(pop_file_.tell, 1);
   BOOST_REQUIRE_EQUAL(pop_value_, value1);
   queue_->pop_chunk(pop_file_, pop_cb_);
   BOOST_REQUIRE_EQUAL(pop_file_.tell, 2);
   BOOST_REQUIRE_EQUAL(pop_value_, value2);
   queue_->pop_chunk(pop_file_, pop_cb_);
   BOOST_REQUIRE(pop_error_); // eof!

   // finally, the delete
   queue_->pop_end(pop_file_, true, pop_end_cb_);

   // should be nothing left to pop
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(pop_error_); // not_found!
}

// test that we can cancel a multi-chunked push
BOOST_FIXTURE_TEST_CASE( test_push_cancel, fixtures::basic_queue )
{
   string value = "I ordered the salmon medium instead of medium well I didn’t want to ruin the magic";
   queue_->push_reserve(push_file_, 2);
   BOOST_REQUIRE_EQUAL(push_file_.tell, 0);

   queue_->push_chunk(push_file_, value, push_cb_);
   BOOST_REQUIRE_EQUAL(push_file_.tell, 1);

   // double-check it's there:
   --push_file_.tell;
   queue_->pop_chunk(push_file_, pop_cb_);
   BOOST_REQUIRE_EQUAL(push_file_.tell, 1); // brought it back up to 1
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_value_, value);

   queue_->push_cancel(push_file_, push_cancel_cb_);
   BOOST_REQUIRE(!push_cancel_error_);

   // make sure it's not there
   --push_file_.tell;
   queue_->pop_chunk(push_file_, pop_cb_);
   BOOST_REQUIRE(pop_error_); // io_error
}

// test that we can push a value that ends in a null-zero
BOOST_FIXTURE_TEST_CASE( test_push_zero, fixtures::basic_queue )
{
   string value = string("I'm sorry Taylor.") + '\0';
   queue_->push(value, push_cb_);
   queue_->pop(0, pop_cb_);
   BOOST_REQUIRE(!push_error_);
   BOOST_REQUIRE(!pop_error_);
   BOOST_REQUIRE_EQUAL(pop_file_.id, 0);
   BOOST_REQUIRE_EQUAL(pop_value_, value);
}

BOOST_AUTO_TEST_SUITE_END()
