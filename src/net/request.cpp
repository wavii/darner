#include "darner/net/request.h"

using namespace boost;
using namespace boost::spirit;
using namespace boost::spirit::ascii;
using namespace darner;

request_grammar::request_grammar()
: request_grammar::base_type(start)
{
   key_name =
      +((qi::alnum|qi::punct) - '/');

   stats =
      no_case[lit("STATS")]     [phoenix::ref(req.type) = request::RT_STATS];

   version =
      no_case[lit("VERSION")]   [phoenix::ref(req.type) = request::RT_VERSION];

   flush =
      no_case[lit("FLUSH ")]    [phoenix::ref(req.type) = request::RT_FLUSH]
      >> key_name               [phoenix::ref(req.queue) = qi::_1];

   flush_all =
      no_case[lit("FLUSH_ALL")] [phoenix::ref(req.type) = request::RT_FLUSH_ALL];

   set =
      no_case[lit("SET ")]      [phoenix::ref(req.type) = request::RT_SET]
      >> key_name               [phoenix::ref(req.queue) = qi::_1]
      >> ' '
      >> qi::uint_ // flags (ignored)
      >> ' '
      >> qi::uint_ // expiration (ignored for now)
      >> ' '
      >> qi::uint_              [phoenix::ref(req.num_bytes) = qi::_1];

   get_option =
      lit("/open")              [phoenix::ref(req.get_open) = true]
      | lit("/close")           [phoenix::ref(req.get_close) = true]
      | lit("/abort")           [phoenix::ref(req.get_abort) = true]
      | (
         lit("/t=")
         >> qi::uint_           [phoenix::ref(req.wait_ms) = qi::_1]
        );

   get = no_case[lit("GET ")]   [phoenix::ref(req.type) = request::RT_GET]
      >> key_name               [phoenix::ref(req.queue) = qi::_1]
      >> *get_option;

   start = (stats | version | flush | flush_all | set | get) >> qi::eol;
}
