#ifndef __TESTS_FIXTURES_BASIC_REQUEST_HPP__
#define __TESTS_FIXTURES_BASIC_REQUEST_HPP__

#include "darner/net/request.h"

namespace fixtures {

// create a single basic request object
class basic_request
{
protected:

   darner::request request_;
   darner::request_parser parser_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_REQUEST_HPP__
