#ifndef __TESTS_FIXTURES_BASIC_REQUEST_HPP__
#define __TESTS_FIXTURES_BASIC_REQUEST_HPP__

#include "darner/request.hpp"

namespace fixtures {

// create a single basic request object
class basic_request
{
public:

   basic_request()
   : grammar_(request_)
   {
   }

protected:

   darner::request request_;
   darner::request_grammar<std::string::const_iterator> grammar_;
};

} // fixtures

#endif // __TESTS_FIXTURES_BASIC_REQUEST_HPP__
