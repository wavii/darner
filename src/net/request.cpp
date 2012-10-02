#include "darner/net/request.h"

#include <boost/version.hpp>

using namespace boost;
using namespace boost::spirit;
using namespace boost::spirit::ascii;
#if BOOST_VERSION < 104100
using namespace boost::spirit::arg_names;
#endif
using namespace darner;

request_grammar::request_grammar()
: request_grammar::base_type(start)
{
   key_name %=
      +((alnum|punct) - '/');

   stats =
      lit("stats")     [phoenix::ref(req.type) = request::RT_STATS];

   version =
      lit("version")   [phoenix::ref(req.type) = request::RT_VERSION];

   flush =
      lit("flush ")    [phoenix::ref(req.type) = request::RT_FLUSH]
      >> key_name      [phoenix::ref(req.queue) = _1];

   flush_all =
      lit("flush_all") [phoenix::ref(req.type) = request::RT_FLUSH_ALL];

   set =
      lit("set ")      [phoenix::ref(req.type) = request::RT_SET]
      >> key_name      [phoenix::ref(req.queue) = _1]
      >> ' '
      >> uint_ // flags (ignored)
      >> ' '
      >> uint_ // expiration (ignored for now)
      >> ' '
      >> uint_         [phoenix::ref(req.num_bytes) = _1];

   get_option =
      lit("/open")     [phoenix::ref(req.get_open) = true]
      | lit("/peek")   [phoenix::ref(req.get_peek) = true]
      | lit("/close")  [phoenix::ref(req.get_close) = true]
      | lit("/abort")  [phoenix::ref(req.get_abort) = true]
      | (
         lit("/t=")
         >> uint_      [phoenix::ref(req.wait_ms) = _1]
        );

   get =
      lit("get")       [phoenix::ref(req.type) = request::RT_GET]
      >> -lit('s') // "gets" is okay too
      >> ' '
      >> key_name      [phoenix::ref(req.queue) = _1]
      >> *get_option
      >> -lit(' '); // be permissive to clients inserting spaces

   start = (stats | version | flush | flush_all | set | get) >> eol;
}
