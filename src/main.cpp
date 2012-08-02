#include <iostream>
#include <fstream>

#include <pthread.h>
#include <signal.h>
#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>

#include "darner/log.h"
#include "darner/server.hpp"

using namespace std;
using namespace boost;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
using namespace darner;

int main(int argc, char * argv[])
{
   po::options_description generic("Generic options");

   string config_path;
   // options only available via command-line
   generic.add_options()
      ("version,v", "prints the version and exits")
      ("help,h", "produce help message")
      ("config,c", po::value<string>(&config_path), "config file path");

   // options allow both on command line and in a config file
   int port;
   string data_path;
   size_t workers;

   po::options_description config("Configuration");
   config.add_options()
      ("debug", "debug (verbose) output")
      ("port,p", po::value<int>(&port)->default_value(22133), "port upon which to listen")
      ("workers,j", po::value<size_t>(&workers)->default_value(1), "number of worker threads")
      ("data,d", po::value<string>(&data_path)->default_value("data"), "data directory")
  ;

   po::options_description cmdline_options;
   cmdline_options.add(generic).add(config);

   po::options_description config_file_options;
   config_file_options.add(config);

   po::variables_map vm;
   po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
   try
   {
      notify(vm);
   }
   catch (const po::error & e)
   {
      cout << "error parsing commandline options: " << e.what() << endl;
      return 1;
   }

   if (vm.count("help"))
   {
      cout << cmdline_options << endl;
      return 0;
   }

   if (vm.count("version"))
   {
      cout << DARNER_VERSION << endl;
      return 0;
   }

   if (vm.count("config"))
   {
      ifstream in(config_path.c_str());
      if (!in)
      {
         cerr << "can't open config file: " << config_path << endl;
         return 1;
      }
      else
      {
         po::store(po::parse_config_file(in, config_file_options), vm);
         try
         {
            notify(vm);
         }
         catch (const po::error & e)
         {
            cerr << "error reading config file: " << e.what() << endl;
            return 1;
         }
      }
   }

   if (!fs::exists(data_path))
   {
      cerr << "cannot find the data directory: " << data_path << endl;
      return 1;
   }

   log::init(vm.count("debug"));

   log::INFO("darner: queue server");
   log::INFO("build: %1% (%2%) v%3% (c) Wavii, Inc.", __DATE__, __TIME__, DARNER_VERSION);
   log::INFO("worker threads: %1%", workers);
   log::INFO("listening on port: %1%", port);
   log::INFO("data dir: %1%", data_path);
   if (vm.count("debug"))
      log::INFO("debug logging is turned ON");

   // TODO: we're doing some manual pthread/signal stuff here
   // migrate to signal_set when we are ready to use boost 1.47 

   sigset_t new_mask;
   sigfillset(&new_mask);
   sigset_t old_mask;
   pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

   log::INFO("starting up");

   server s(data_path, port, workers);

   s.start();

   // Restore previous signals.
   pthread_sigmask(SIG_SETMASK, &old_mask, 0);

   // Wait for signal indicating time to shut down.
   sigset_t wait_mask;
   sigemptyset(&wait_mask);
   sigaddset(&wait_mask, SIGINT);
   sigaddset(&wait_mask, SIGQUIT);
   sigaddset(&wait_mask, SIGTERM);
   pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
   int sig = 0;
   sigwait(&wait_mask, &sig);

   // Stop the server.
   log::INFO("received signal. stopping server and finishing work.");

   s.stop();
   s.join();

   log::INFO("shutting down");

   return 0;
}
