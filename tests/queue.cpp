#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/test/test_tools.hpp>

#include "darner/queue.hpp"
#include "fixtures/basic.hpp"

using namespace std;

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

BOOST_AUTO_TEST_SUITE_END()
