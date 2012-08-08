#include <iostream>

#include <string>
#include <vector>

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
   : get_set_ratio(static_cast<float>(gets) / sets), item_size(_item_size), gets_remaining(gets), sets_remaining(sets)
   {
      string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()";
      ostringstream oss;
      oss << "set test_queue 0 0 " << item_size << "\r\n";
      for (; item_size != 0; --item_size)
         oss << chars[rand() % chars.size()];
      oss << "\r\n";
      set_request = oss.str();
      get_request = "get test_queue\r\n";
      times.reserve(gets_remaining + sets_remaining);
   }

   string set_request;
   string get_request;
   vector<unsigned int> times;
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
         bind(&session::do_get_or_set, shared_from_this(), _1));
   }

private:

   void do_get_or_set(const system::error_code& e)
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
      async_read_until(socket_, in_, '\n', bind(&session::get_on_header, shared_from_this(), _1, _2));
   }

   void get_on_header(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return;
      asio::streambuf::const_buffers_type bufs = in_.data();
      std::string header(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
      in_.consume(bytes_transferred);
      if (header == "END\r\n")
         do_get_or_set(system::error_code());
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
         return;
      results_.times.push_back((posix_time::microsec_clock::local_time() - begin_).total_nanoseconds());
      in_.consume(in_.size());
      do_get_or_set(system::error_code());
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
         return;
      size_t required = 8; // STORED\r\n
      async_read(
         socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
         bind(&session::set_on_response, shared_from_this(), _1, _2));
   }

   void set_on_response(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return;
      results_.times.push_back((posix_time::microsec_clock::local_time() - begin_).total_nanoseconds());
      in_.consume(in_.size());
      do_get_or_set(system::error_code());
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
   ios.run();

   cout << r.gets_remaining << endl;

   return 0;
}
