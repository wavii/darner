#include <iostream>

#include <string>
#include <vector>
#include <algorithm>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
namespace po = boost::program_options;

struct results
{
   results(size_t _item_size, size_t gets, size_t sets)
   : bytes_in(0),
     bytes_out(0),
     get_set_ratio(static_cast<float>(gets) / sets),
     item_size(_item_size),
     gets_remaining(gets),
     sets_remaining(sets)
   {
      string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()";
      ostringstream oss;
      oss << "set db_bench 0 0 " << item_size << "\r\n";
      for (; item_size != 0; --item_size)
         oss << chars[rand() % chars.size()];
      oss << "\r\n";
      set_request = oss.str();
      get_request = "get db_bench\r\n";
      times.reserve(gets_remaining + sets_remaining);
   }

   string set_request;
   string get_request;
   vector<unsigned int> times;
   size_t bytes_in;
   size_t bytes_out;
   float get_set_ratio;
   size_t item_size;
   size_t gets_remaining;
   size_t sets_remaining;
};

class session : public enable_shared_from_this<session>
{
public:

   typedef ip::tcp::socket socket_type;
   typedef shared_ptr<session> ptr_type;

   session(io_service& ios, results& _results, const std::string& host, size_t port)
   : socket_(ios),
     ios_(ios),
     results_(_results),
     host_(host),
     port_(port)
   {
   }

   void start()
   {
      socket_.async_connect(
         ip::tcp::endpoint(ip::address::from_string(host_), port_),
         bind(&session::on_connect, shared_from_this(), _1));
   }

private:

   void fail(const std::string& method, const system::error_code& e)
   {
      cerr << "[ERROR] " << method << ": " << e.message() << endl;
   }

   void on_connect(const system::error_code& e)
   {
      if (e)
         return fail("on_connect", e);

      socket_.set_option(ip::tcp::no_delay(true));
      do_get_or_set();
   }

   void do_get_or_set()
   {
      if (results_.gets_remaining)
      {
         if (!results_.sets_remaining)
            do_get();
         else if (static_cast<float>(results_.gets_remaining) / results_.sets_remaining > results_.get_set_ratio)
            do_get();
         else
            do_set();
      }
      else if (results_.sets_remaining)
         do_set();
   }

   void do_get()
   {
      --results_.gets_remaining;
      begin_ = posix_time::microsec_clock::local_time();

      async_write(socket_, buffer(results_.get_request), bind(&session::get_on_write, shared_from_this(), _1, _2));
   }

   void get_on_write(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("get_on_write", e);

      results_.bytes_out += bytes_transferred;

      async_read_until(socket_, in_, '\n', bind(&session::get_on_header, shared_from_this(), _1, _2));
   }

   void get_on_header(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("get_on_header", e);

      results_.bytes_in += bytes_transferred;

      asio::streambuf::const_buffers_type bufs = in_.data();
      std::string header(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
      in_.consume(bytes_transferred);

      if (header == "END\r\n")
         do_get_or_set();
      else
      {
         size_t required = results_.item_size + 7; // item + \r\nEND\r\n
         async_read(
            socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
            bind(&session::get_on_value, shared_from_this(), _1, _2));
      }
   }

   void get_on_value(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("get_on_value", e);

      results_.times.push_back((posix_time::microsec_clock::local_time() - begin_).total_microseconds());
      results_.bytes_in += bytes_transferred;

      in_.consume(in_.size());
      do_get_or_set();
   }

   void do_set()
   {
      --results_.sets_remaining;
      begin_ = boost::posix_time::microsec_clock::local_time();

      async_write(socket_, buffer(results_.set_request), bind(&session::set_on_write, shared_from_this(), _1, _2));
   }

   void set_on_write(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("set_on_write", e);

      results_.bytes_out += bytes_transferred;

      size_t required = 8; // STORED\r\n
      async_read(
         socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
         bind(&session::set_on_response, shared_from_this(), _1, _2));
   }

   void set_on_response(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("set_on_response", e);

      results_.times.push_back((posix_time::microsec_clock::local_time() - begin_).total_microseconds());
      results_.bytes_in += bytes_transferred;

      in_.consume(in_.size());
      do_get_or_set();
   }   

   socket_type socket_;
   asio::streambuf in_;
   posix_time::ptime begin_;

   io_service& ios_;
   results& results_;
   string host_;
   size_t port_;
};

int main(int argc, char * argv[])
{
   po::options_description generic("Generic options");

   string host;
   size_t port, num_gets, num_sets, item_size, workers;

   // options only available via command-line
   generic.add_options()
      ("version,v", "prints the version and exits")
      ("help", "produce help message")
      ("host,h", po::value<string>(&host)->default_value("127.0.0.1"), "address to connect")
      ("port,p", po::value<size_t>(&port), "port to establish connections")
      ("gets,g", po::value<size_t>(&num_gets)->default_value(10000), "number of gets to issue")
      ("sets,s", po::value<size_t>(&num_sets)->default_value(10000), "number of sets to issue")
      ("item_size", po::value<size_t>(&item_size)->default_value(1024), "default size of item")
      ("concurrency,c", po::value<size_t>(&workers)->default_value(10), "number of concurrent workers");

   po::options_description cmdline_options;
   cmdline_options.add(generic);

   po::variables_map vm;
   try
   {
      po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
      notify(vm);
   }
   catch (const po::error & e)
   {
      cout << "error parsing commandline options: " << e.what() << endl;
      cout << cmdline_options << endl;
      return 1;
   }

   if (vm.count("help"))
   {
      cout << "db: darner bench.  performs benchmarks of concurrent gets and sets of a darner server." << endl;
      cout << "usage: db -p <port>" << endl;
      cout << cmdline_options << endl;
      return 0;
   }

   if (vm.count("version"))
   {
      cout << DARNER_VERSION << endl;
      return 0;
   }

   if (!vm.count("port"))
   {
      cout << "please specify a port (-p)" << endl;
      cout << "db --help for help" << endl;
      return 1;
   }

   results r(item_size, num_gets, num_sets);
   io_service ios;
   for (size_t i = 0; i != workers; ++i)
      session::ptr_type(new session(ios, r, host, port))->start();

   posix_time::ptime begin = posix_time::microsec_clock::local_time();

   ios.run();

   posix_time::ptime end = posix_time::microsec_clock::local_time();

   float elapsed_seconds = (end - begin).total_milliseconds() / 1000.0;

   sort(r.times.begin(), r.times.end());

   cout << left;
   cout << setw(24) << "Concurrency Level:"  << workers << endl;
   cout << setw(24) << "Gets:" << num_gets << endl;
   cout << setw(24) << "Sets:" << num_sets << endl;
   cout << setw(24) << "Time taken for tests:" << elapsed_seconds << " seconds" << endl;
   cout << setw(24) << "Bytes read:" << r.bytes_in << " bytes" << endl;
   cout << setw(24) << "Read rate:" << r.bytes_in / elapsed_seconds / 1024  << " Kbytes/sec" << endl;
   cout << setw(24) << "Bytes written:" << r.bytes_out << " bytes" << endl;
   cout << setw(24) << "Write rate:" << r.bytes_out / elapsed_seconds / 1024  << " Kbytes/sec" << endl;

   if (!r.times.empty())
   {
      float avg = 0;

      for (vector<unsigned int>::const_iterator it = r.times.begin(); it != r.times.end(); ++it)
         avg += *it;
      avg /= r.times.size();

      cout << setw(24) << "Time per request:" << avg << " [us] (mean)" << endl << endl;
      cout << "Percentage of the requests served within a certain time (us)" << endl;
      cout << setw(10) << "  50%:" << r.times[r.times.size() * 50 / 100] << endl;
      cout << setw(10) << "  66%:" << r.times[r.times.size() * 66 / 100] << endl;
      cout << setw(10) << "  75%:" << r.times[r.times.size() * 75 / 100] << endl;
      cout << setw(10) << "  80%:" << r.times[r.times.size() * 80 / 100] << endl;
      cout << setw(10) << "  90%:" << r.times[r.times.size() * 90 / 100] << endl;
      cout << setw(10) << "  95%:" << r.times[r.times.size() * 95 / 100] << endl;
      cout << setw(10) << "  98%:" << r.times[r.times.size() * 98 / 100] << endl;
      cout << setw(10) << "  99%:" << r.times[r.times.size() * 99 / 100] << endl;
      cout << setw(10) << " 100%:" << r.times[r.times.size() - 1] << " (longest request)" << endl;
   }



   return 0;
}
