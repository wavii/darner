// darner bench: similar to apache bench: reports statistics for gets/sets over concurrent connections

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

class session : public boost::enable_shared_from_this<session>
{
public:

   typedef ip::tcp::socket socket_type;
   typedef boost::shared_ptr<session> ptr_type;

   session(io_service& ios, results& _results, const string& host, size_t port, size_t bad_client_rate,
      size_t action = 0)
   : socket_(ios),
     ios_(ios),
     results_(_results),
     host_(host),
     port_(port),
     bad_client_rate_(bad_client_rate),
     action_(action)
   {
      socket_.connect(ip::tcp::endpoint(ip::address::from_string(host_), port_));
      socket_.set_option(ip::tcp::no_delay(true));
   }

   void start()
   {
      ios_.post(bind(&session::do_get_or_set, shared_from_this()));
   }

private:

   void fail(const string& method, const string& message)
   {
      cerr << "[ERROR] " << method << ": " << message << endl;
   }

   void fail(const string& method, const system::error_code& e)
   {
      cerr << "[ERROR] " << method << ": " << e.message() << endl;
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

      // gets can be bad in two ways: bad send, bad recv
      bool bad_client = bad_client_rate_ && !(action_++ % bad_client_rate_);
      size_t buf_len = bad_client ? results_.get_request.size() / 2 : results_.get_request.size();
      
      async_write(
         socket_, buffer(results_.get_request, buf_len), bind(&session::get_on_write, shared_from_this(), _1, _2));
   }

   void get_on_write(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("get_on_write", e);

      results_.bytes_out += bytes_transferred;

      if (bytes_transferred < results_.get_request.size()) // bad client?  hang up and reconnect
         session::ptr_type(new session(ios_, results_, host_, port_, bad_client_rate_, action_))->start();
      else
         async_read_until(socket_, in_, '\n', bind(&session::get_on_header, shared_from_this(), _1, _2));
   }

   void get_on_header(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("get_on_header", e);

      bool bad_client = bad_client_rate_ && !(action_++ % bad_client_rate_);
      if (bad_client)
      {
         session::ptr_type(new session(ios_, results_, host_, port_, bad_client_rate_, action_))->start();
         return;
      }

      results_.bytes_in += bytes_transferred;

      asio::streambuf::const_buffers_type bufs = in_.data();
      string header(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
      in_.consume(bytes_transferred);

      if (header == "END\r\n")
      {
         results_.times.push_back((posix_time::microsec_clock::local_time() - begin_).total_microseconds());
         do_get_or_set();
      }
      else
      {
         size_t pos = header.rfind(' ');
         if (pos == string::npos)
            return fail("get_on_header", "bad header");
         size_t required = atoi(header.c_str() + pos + 1) + 7; // + \r\nEND\r\n
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
      results_.bytes_in += in_.size();

      in_.consume(in_.size());
      do_get_or_set();
   }

   void do_set()
   {
      --results_.sets_remaining;
      begin_ = boost::posix_time::microsec_clock::local_time();

      bool bad_client = bad_client_rate_ && !(action_++ % bad_client_rate_);
      size_t buf_len = bad_client ? results_.set_request.size() / 2 : results_.set_request.size();

      async_write(
         socket_, buffer(results_.set_request, buf_len), bind(&session::set_on_write, shared_from_this(), _1, _2));
   }

   void set_on_write(const system::error_code& e, size_t bytes_transferred)
   {
      if (e)
         return fail("set_on_write", e);

      results_.bytes_out += bytes_transferred;

      if (bytes_transferred < results_.set_request.size()) // bad client?  hang up and reconnect
      {
         session::ptr_type(new session(ios_, results_, host_, port_, bad_client_rate_, action_))->start();
         return;
      }

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

      in_.consume(bytes_transferred);
      do_get_or_set();
   }   

   socket_type socket_;
   asio::streambuf in_;
   posix_time::ptime begin_;

   io_service& ios_;
   results& results_;
   string host_;
   size_t port_;
   size_t bad_client_rate_;
   size_t action_;
};

int main(int argc, char * argv[])
{
   po::options_description generic("Generic options");

   string host;
   size_t port, num_gets, num_sets, item_size, workers, bad_client_rate;

   // options only available via command-line
   generic.add_options()
      ("version,v", "prints the version and exits")
      ("help", "produce help message")
      ("host,h", po::value<string>(&host)->default_value("127.0.0.1"), "address to connect")
      ("port,p", po::value<size_t>(&port), "port to establish connections")
      ("gets,g", po::value<size_t>(&num_gets)->default_value(10000), "number of gets to issue")
      ("sets,s", po::value<size_t>(&num_sets)->default_value(10000), "number of sets to issue")
      ("item_size,i", po::value<size_t>(&item_size)->default_value(64), "default size of item")
      ("concurrency,c", po::value<size_t>(&workers)->default_value(10), "number of concurrent workers")
      ("bad_client_rate,b", po::value<size_t>(&bad_client_rate)->default_value(0), "0 = never, 1 = always, 2 = 1/2 sets/gets are bad, 3 = 1/3 sets/gets are bad...");

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
      session::ptr_type(new session(ios, r, host, port, bad_client_rate))->start();

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

      cout << setw(24) << "Requests per second:" << (num_gets + num_sets) / elapsed_seconds << " #/sec (mean)" << endl;
      cout << setw(24) << "Time per request:" << avg << " us (mean)" << endl << endl;
      cout << "Percentage of the requests served within a certain time (us)" << endl;

      int percentages[] = {50, 66, 75, 80, 90, 95, 98, 99};
      for (size_t i = 0; i < sizeof(percentages) / sizeof(int); ++i)
         cout << "  " <<  percentages[i] << "%:    " << r.times[r.times.size() * percentages[i] / 100] << endl;
      cout << " 100%:    " << r.times[r.times.size() - 1] << " (longest request)" << endl;
   }

   return 0;
}
