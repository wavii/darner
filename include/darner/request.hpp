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
      RT_DELETE    = 5,
      RT_SET       = 6,
      RT_GET       = 7
   };

   request_type type;
   std::string queue;
   size_t num_bytes;
   bool get_open;
   bool get_close;
   bool get_abort;
   size_t wait_ms;
};

template <class Iterator>
struct request_grammar : boost::spirit::qi::grammar<Iterator>
{
   request_grammar(request& req)
   : request_grammar::base_type(start),
     req_(req)
   {
      using namespace boost::spirit;
      using namespace boost;

      key_name =
         +((qi::alnum|qi::punct) - '/');

      stats =
         lit("STATS")     [phoenix::ref(req_.type) = request::RT_STATS];

      version =
         lit("VERSION")   [phoenix::ref(req_.type) = request::RT_VERSION];

      flush =
         lit("FLUSH ")    [phoenix::ref(req_.type) = request::RT_FLUSH]
         >> key_name      [phoenix::ref(req_.queue) = qi::_1];

      flush_all =
         lit("FLUSH_ALL") [phoenix::ref(req_.type) = request::RT_FLUSH_ALL];

      set =
         lit("SET ")      [phoenix::ref(req_.type) = request::RT_SET]
         >> key_name      [phoenix::ref(req_.queue) = qi::_1]
         >> ' '
         >> qi::uint_     [phoenix::ref(req_.num_bytes) = qi::_1];

      get_option =
         lit("/open")     [phoenix::ref(req_.get_open) = true]
         | lit("/close")  [phoenix::ref(req_.get_close) = true]
         | lit("/abort")  [phoenix::ref(req_.get_abort) = true]
         | (
            lit("/t=")
            >> qi::uint_  [phoenix::ref(req_.wait_ms) = qi::_1]
           );

      get = lit("GET ")   [phoenix::ref(req_.type) = request::RT_GET]
         >> key_name      [phoenix::ref(req_.queue) = qi::_1]
         >> *get_option;

      start = (stats | version | flush | flush_all | set | get) >> qi::eol;
   }

   bool parse(Iterator begin, Iterator end)
   {
      new(&req_) request(); // spooky placement new!
      return boost::spirit::qi::parse(begin, end, *this) && (begin == end);
   }

   template <class Sequence>
   bool parse(const Sequence& seq)
   {
      return parse(seq.begin(), seq.end());
   }

   request& req_;
   boost::spirit::qi::rule<Iterator, std::string()> key_name;
   boost::spirit::qi::rule<Iterator> stats, version, flush, flush_all, set, get_option, get, start;
};

} // darner

#endif // __DARNER_REQUEST_HPP__
