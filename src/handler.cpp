#include "darner/handler.h"

using namespace std;
using namespace boost;
using namespace darner;

request_handler::request_handler(const string& data_path)
: data_path_(data_path)
{

}

void request_handler::handle_stats(request_handler::socket_type& socket, const request_handler::response_callback& cb)
{

}
