#ifndef __DARNER_REQUEST_HPP__
#define __DARNER_REQUEST_HPP__

#include <string>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>

namespace darner {

struct request
{
   enum request_type
   {
      RT_STATS     = 1,
      RT_VERSION   = 2,
      RT_FLUSH     = 3,
      RT_FLUSH_ALL = 4,
      RT_SET       = 5,
      RT_GET       = 6
   };

   request_type type;
   std::string queue;
   size_t num_bytes;
   bool get_open;
   bool get_peek;
   bool get_close;
   bool get_abort;
   bool set_sync;
   size_t wait_ms;
};

struct request_grammar : boost::spirit::qi::grammar<std::string::const_iterator>
{
   request_grammar();
   request req;
   boost::spirit::qi::rule<std::string::const_iterator, std::string()> key_name;
   boost::spirit::qi::rule<std::string::const_iterator> stats, version, flush, flush_all, set_option, set, get_option, get, start;
};

// grammar are expensive to construct.  to be thread-safe, let's make one grammar per thread.
class request_parser
{
public:

   bool parse(request& req, std::string::const_iterator begin, std::string::const_iterator end)
   {
      grammar_.req = request();
      bool success = boost::spirit::qi::parse(begin, end, grammar_) && (begin == end);
      if (success)
         req = grammar_.req;

      return success;
   }

   template <class Sequence>
   bool parse(request& req, const Sequence& seq)
   {
      return parse(req, seq.begin(), seq.end());
   }

private:

   request_grammar grammar_;
};

} // darner

#endif // __DARNER_REQUEST_HPP__
