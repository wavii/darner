#include <iostream>
#include <sstream>

#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <darner/queue/queue.h>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace darner;

class event_loop
{
public:

   event_loop(size_t item_size, size_t num_pushpops)
   : push_cb_(bind(&event_loop::push_cb, this, _1, _2)),
     pop_cb_(bind(&event_loop::pop_cb, this, _1, _2, _3)),
     pop_end_cb_(bind(&event_loop::pop_end_cb, this, asio::placeholders::error)),
     pushes_(num_pushpops),
     pops_(num_pushpops),
     q_(ios_, "tmp")
   {
      string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()";
      ostringstream oss;
      for (; item_size != 0; --item_size)
         oss << chars[rand() % chars.size()];
      value_ = oss.str();
   }

   void go()
   {
      q_.push(value_, push_cb_);
      q_.pop(0, pop_cb_);
      ios_.run();
   }

private:

   void push_cb(const system::error_code& error, const file_type& file)
   {
      if (--pushes_ > 0)
         ios_.post(bind(&queue::push, &q_, ref(value_), push_cb_));
   }

   void pop_cb(const system::error_code& error, const file_type& file, std::string& value)
   {
      if (!error)
         q_.pop_end(file, true, pop_end_cb_);
      if (--pops_ > 0)
         ios_.post(bind(&queue::pop, &q_, 0, pop_cb_));
   }

   void pop_end_cb(const system::error_code& error)
   {
   }

   queue::push_callback push_cb_;
   queue::pop_callback pop_cb_;
   queue::success_callback pop_end_cb_;
   string value_;
   size_t pushes_;
   size_t pops_;
   io_service ios_;
   queue q_;
};

// tests queue: simple flood, one producer one consumer
int main(int argc, char * argv[])
{
   if (argc != 3)
   {
      cerr << "queue-flood: reports in ms how long it takes to drive <num-pushpops> push/pops" << endl;
      cerr << "of items length <item-size> with one producer and one consumer" << endl;
      cerr << "usage: " << argv[0] << " <item-size> <num-pushpops>" << endl;
      return 1;
   }

   size_t item_size = lexical_cast<size_t>(argv[1]);
   size_t num_pushpops = lexical_cast<size_t>(argv[2]);

   event_loop e(item_size, num_pushpops);

   posix_time::ptime start(posix_time::microsec_clock::local_time());

   e.go();

   posix_time::ptime end(posix_time::microsec_clock::local_time());

   cout << "item size: " << item_size << endl;
   cout << num_pushpops << " pushes/pops took " << (end - start).total_milliseconds() << " ms" << endl;
   cout << static_cast<float>((end - start).total_microseconds()) / num_pushpops << " us per push/pop " << endl;

	return 0;
}
