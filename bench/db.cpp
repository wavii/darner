#include <iostream>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
namespace po = boost::program_options;

struct results
{
   results(size_t item_size)
   {
      string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()";
      ostringstream oss;
      for (; item_size != 0; --item_size)
         oss << chars[rand() % chars.size()];
      item = oss.str();
   }

   string item;
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
         bind(&session::handle_connect, shared_from_this(), _1));
   }

private:

   void handle_connect(const system::error_code& e)
   {

   }

   socket_type socket_;

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

   results r(item_size);
   io_service ios;
   for (size_t i = 0; i != workers; ++i)
      session::ptr_type(new session(ios, r, host, port))->start();
   ios.run();

   return 0;
}
