#include <iostream>

#include <boost/thread.hpp>
#include <boost/program_options.hpp>

using namespace std;
using namespace boost;
namespace po = boost::program_options;

int main(int argc, char * argv[])
{
   po::options_description generic("Generic options");

   size_t port, num_gets, num_sets, workers;

   // options only available via command-line
   generic.add_options()
      ("version,v", "prints the version and exits")
      ("help,h", "produce help message")
      ("port,p", po::value<size_t>(&port), "port to establish connections")
      ("gets,g", po::value<size_t>(&num_gets)->default_value(10000), "number of gets to issue")
      ("sets,s", po::value<size_t>(&num_sets)->default_value(10000), "number of sets to issue")
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

   cout << "woohoo" << endl;

   return 0;
}
