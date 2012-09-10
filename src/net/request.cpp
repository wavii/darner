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
      lit("stats")     [phoenix::ref(req.type) = request::RT_STATS];

   version =
      lit("version")   [phoenix::ref(req.type) = request::RT_VERSION];

   flush =
      lit("flush ")    [phoenix::ref(req.type) = request::RT_FLUSH]
      >> key_name      [phoenix::ref(req.queue) = qi::_1];

   flush_all =
      lit("flush_all") [phoenix::ref(req.type) = request::RT_FLUSH_ALL];

   set =
      lit("set ")      [phoenix::ref(req.type) = request::RT_SET]
      >> key_name      [phoenix::ref(req.queue) = qi::_1]
      >> ' '
      >> qi::uint_ // flags (ignored)
      >> ' '
      >> qi::uint_ // expiration (ignored for now)
      >> ' '
      >> qi::uint_     [phoenix::ref(req.num_bytes) = qi::_1];

   get_option =
      lit("/open")     [phoenix::ref(req.get_open) = true]
      | lit("/peek")   [phoenix::ref(req.get_peek) = true]
      | lit("/close")  [phoenix::ref(req.get_close) = true]
      | lit("/abort")  [phoenix::ref(req.get_abort) = true]
      | (
         lit("/t=")
         >> qi::uint_  [phoenix::ref(req.wait_ms) = qi::_1]
        );

   get =
      lit("get")       [phoenix::ref(req.type) = request::RT_GET]
      >> -lit('s') // "gets" is okay too
      >> ' '
      >> key_name      [phoenix::ref(req.queue) = qi::_1]
      >> *get_option
      >> *lit(' '); // be permissive to clients inserting spaces

   start = (stats | version | flush | flush_all | set | get) >> qi::eol;
}
