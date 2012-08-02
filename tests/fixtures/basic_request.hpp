#ifndef __TESTS_FIXTURES_BASIC_REQUEST_HPP__
#define __TESTS_FIXTURES_BASIC_REQUEST_HPP__

#include "darner/request.hpp"

namespace fixtures {

// create a single basic request object
class basic_request
{
protected:

   darner::request request_;
   darner::request_parser<std::string::const_iterator> parser_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_REQUEST_HPP__
