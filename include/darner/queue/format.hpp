#ifndef __DARNER_FORMAT_HPP__
#define __DARNER_FORMAT_HPP__

#include <vector>
#include <string>

#include <leveldb/db.h>

namespace darner {

typedef boost::uint64_t id_type;
typedef boost::uint64_t size_type;

// queue item points to chunk item via a small metadata header
class header_type
{
public:

   header_type() : chunk_beg(0), chunk_end(0), size(0) {}
   header_type(id_type _chunk_beg, id_type _chunk_end, size_type _size)
   : chunk_beg(_chunk_end), chunk_end(_chunk_end), size(_size) {}
   header_type(const std::string& buf)
   {
      *this = *reinterpret_cast<const header_type*>(buf.c_str());
   }

   id_type chunk_beg;
   id_type chunk_end;
   size_type size;

   const std::string& str() const
   {
      buf_ = std::string(reinterpret_cast<const char *>(this), sizeof(header_type)) + '\1' + '\0';
      return buf_;
   }

private:

   mutable std::string buf_;
};

// every queue item gets a file_type.  use the file_type to coordinate across multiple calls
struct file_type
{
   file_type() : id(0), header(), tell(0) {}
   file_type(id_type _id, header_type _header, size_type _tell)
   : id(_id), header(_header), tell(_tell) {}

   id_type id;
   header_type header;
   size_type tell; // points to chunk write/read head
};

class key_type
{
public:

   enum // a key can be either a queue or a chunk type
   {
      KT_QUEUE = 1,
      KT_CHUNK = 2
   };

   key_type() : type(KT_QUEUE), id(0) {}

   key_type(char _type, id_type _id) : type(_type), id(_id) {}

   key_type(const leveldb::Slice& s)
   : type(s.data()[sizeof(id_type)]), id(*reinterpret_cast<const id_type*>(s.data())) {}

   leveldb::Slice slice() const
   {
      buf_.resize(sizeof(id_type) + 1);
      *reinterpret_cast<id_type*>(&buf_[0]) = id;
      buf_[sizeof(id_type)] = type;
      return leveldb::Slice(&buf_[0], buf_.size());
   }

   bool operator< (const key_type& other) const
   {
      if (type < other.type)
         return -1;
      else if (type > other.type)
         return 1;
      else
      {
         if (id < other.id)
            return -1;
         else if (id > other.id)
            return 1;
         else
            return 0;
      }
   }

   unsigned char type;
   id_type id;

private:

   mutable std::vector<char> buf_;
};

} // darner

#endif // __DARNER_FORMAT_HPP__
