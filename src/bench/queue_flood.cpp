#include <iostream>
#include <darner/queue.hpp>

using namespace std;
using namespace boost::asio;
using namespace darner;

#define ITEMCOUNT 10000
static string value;

void push_cb(queue& dq, size_t i, const boost::system::error_code& error)
{
   if (i < ITEMCOUNT)
      dq.push(value, boost::bind(push_cb, boost::ref(dq), i + 1, boost::asio::placeholders::error));
}

void pop_end_cb(const boost::system::error_code& error)
{
}

void pop_cb(queue& dq, size_t i, const boost::system::error_code& error, queue::key_t key, const std::string& value)
{
   if (!error)
      dq.pop_end(key, true, &pop_end_cb);
   if (i < ITEMCOUNT)
      dq.pop(0, boost::bind(&pop_cb, boost::ref(dq), i + 1, _1, _2, _3));
}

// tests queue: simple flood, one producer one consumer
int main(int argc, char * argv[])
{
   std::cin >> value;
   std::cout << value.size() << std::endl;
	io_service ios;
	queue dq(ios, "poop");

	dq.push(value, boost::bind(&push_cb, boost::ref(dq), 0, boost::asio::placeholders::error));
   dq.pop(0, boost::bind(&pop_cb, boost::ref(dq), 0, _1, _2, _3));

	ios.run();

	return 0;
}