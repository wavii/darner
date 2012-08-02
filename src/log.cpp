#include "darner/log.h"

#include <iostream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string/predicate.hpp>

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace darner;

log::log(mutex& mutex, ostream& out, const string& tag)
: mutex_(mutex), out_(out), tag_(tag), enabled_(false)
{
}

void log::enable(bool enabled)
{
   mutex::scoped_lock lock(mutex_);
   enabled_ = enabled;
}

void log::operator()(const string& format) const
{
   if (!enabled_)
      return;

   ptime now(microsec_clock::local_time());
   mutex::scoped_lock lock(mutex_);
   out_ << "[" << tag_ << "] " << now << ": " << format;
   if (boost::algorithm::ends_with(format, "...")) // stupid hack, don't newline on "..."
      out_ << flush;
   else
      out_ << endl;
}

void darner::log::init(bool debug /* = false*/)
{
   darner::log::DEBUG.enable(debug);
   darner::log::INFO.enable(true);
   darner::log::ERROR.enable(true);
}

boost::mutex cout_mutex;
boost::mutex cerr_mutex;

darner::log darner::log::DEBUG(cerr_mutex, cerr, "DEBUG");
darner::log darner::log::INFO(cout_mutex, cout, "INFO");
darner::log darner::log::ERROR(cerr_mutex, cerr, "ERROR");
