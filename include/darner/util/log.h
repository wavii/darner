// poor man's logging.  come on boost!  finish your logging already!  we've been waiting since 2007

#ifndef __DARNER_LOG_H__
#define __DARNER_LOG_H__

#include <ostream>
#include <vector>
#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/format.hpp>
#include <leveldb/db.h>

#ifdef WIN32
// defined in wingdi.h
#undef ERROR 
#endif

namespace darner {

class log
{
public:

   static log DEBUG;
   static log INFO;
   static log ERROR;

   static void init(bool debug = false);

   log(boost::mutex& mutex, std::ostream& out, const std::string& tag);

   void enable(bool enabled);

   bool is_enabled()
   { return enabled_; }

   void operator()(const std::string& format) const;

   template<class T1>
   void operator()(const std::string& format, const T1& v1) const
   {
      if (enabled_)
         operator()((boost::format(format) % v1).str());
   }

   template<class T1, class T2>
   void operator()(const std::string& format, const T1& v1, const T2& v2) const
   {
      if (enabled_)
         operator()((boost::format(format) % v1 % v2).str());
   }

   template<class T1, class T2, class T3>
   void operator()(const std::string& format, const T1& v1, const T2& v2, const T3& v3) const
   {
      if (enabled_)
         operator()((boost::format(format) % v1 % v2 % v3).str());
   }

   template<class T1, class T2, class T3, class T4>
   void operator()(const std::string& format, const T1& v1, const T2& v2, const T3& v3, const T4& v4) const
   {
      if (enabled_)
         operator()((boost::format(format) % v1 % v2 % v3 % v4).str());
   }

private:

   boost::mutex& mutex_;
   std::ostream& out_;
   std::string tag_;

   bool enabled_;
};

} // darner

#endif // __DARNER_LOG_H__
