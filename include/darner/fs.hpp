#ifndef __DARNER_FS_HPP__
#define __DARNER_FS_HPP__

#include <stdexcept>

#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/write_batch.h>
#include <leveldb/comparator.h>

namespace darner {

/*
 * fs is a blob store. a blob is a series of chunks.  blobs can be read in a chunk at a time via an istream,
 * or written out a chunk at a time via on ostream.  fs provides a predictable upper bound to the amount of memory
 * required to store a large item (at most, the size of a chunk), and the amount of time needed to process an event
 * (at most, the time it takes to write a chunk-size value to leveldb),
 *
 * fs is not thread-safe, it assumes a single-thread calling and operating the provided io_service
 */
class fs
{
public:

   class istream;
   class ostream;
   typedef boost::uint64_t key_type;
   typedef boost::uint64_t size_type;
   typedef std::pair<key_type, size_type> chunkptr_type; // key and chunk index

   // every blob has multiple chunks.  the first chunk of every blob (chunk index zero) is a 16 byte header that
   // records the total size of the blob and how many chunks (including the header chunk) it encompasses
   struct header
   {
      header(size_type _size, size_type _chunks): size(_size), chunks(_chunks) {}
      size_type size;
      size_type chunks;
   };

   fs(boost::asio::io_service& ios,
      const std::string& path)
   : chunks_(NULL),
     cmp_(new comparator()),
     tail_(0),
     ios_(ios)
   {
      leveldb::Options options;
      options.create_if_missing = true;
      options.comparator = cmp_.get();
      leveldb::DB* pdb;
      if (!leveldb::DB::Open(options, path, &pdb).ok())
         throw std::runtime_error("can't open chunk store: " + path);
      chunks_.reset(pdb);
      // tail just marks a convenient way to track a unique identifier for each new key
      boost::scoped_ptr<leveldb::Iterator> it(chunks_->NewIterator(leveldb::ReadOptions()));
      it->SeekToLast();
      if (it->Valid())
         tail_ = *reinterpret_cast<const key_type *>(it->key().data()) + 1;
   }

   class istream
   {
   public:

      friend class fs;

      typedef boost::function<void (const boost::system::error_code& error, const std::string& value)> read_callback;
      typedef boost::function<void (const boost::system::error_code& error)> remove_callback;

      istream(fs& _fs, key_type key)
      : fs_(_fs),
        ptr_(key, 0),
        header_(0, 0)
      {
         leveldb::Slice skey(reinterpret_cast<const char*>(&ptr_), sizeof(chunkptr_type));
         if (fs_.chunks_->Get(leveldb::ReadOptions(), skey, &value_).ok())
         {
            header_ = *reinterpret_cast<const header*>(value_.c_str());
            ++ptr_.second;
         }
      }

      key_type key() const
      {
         return ptr_.first;
      }

      void read(const read_callback& cb)
      {
         // if ptr hasn't been aligned to the first data chunk, it means we couldn't find this key
         boost::system::error_code e;
         if (!ptr_.second)
            e = boost::asio::error::not_found;
         else if (ptr_.second > header_.chunks)
            e = boost::asio::error::eof;
         else
         {
            leveldb::Slice skey(reinterpret_cast<const char*>(&ptr_), sizeof(chunkptr_type));
            if (!fs_.chunks_->Get(leveldb::ReadOptions(), skey, &value_).ok())
               e = boost::system::error_code(boost::system::errc::io_error, boost::system::system_category());
            else
               ++ptr_.second;
         }
         cb(e, value_);
      }

      void remove(const remove_callback& cb)
      {
         leveldb::WriteBatch batch;
         for (chunkptr_type it(ptr_.first, 0); it.second <= header_.chunks; ++it.second)
            batch.Delete(leveldb::Slice(reinterpret_cast<const char *>(&it), sizeof(chunkptr_type)));
         if (!fs_.chunks_->Write(leveldb::WriteOptions(), &batch).ok())
            cb(boost::system::error_code(boost::system::errc::io_error, boost::system::system_category()));
         else
            cb(boost::system::error_code());
      }

      size_type size() const
      {
         return header_.size;
      }

   private:

      fs& fs_;
      chunkptr_type ptr_;
      header header_;
      std::string value_;
   };

   class ostream
   {
   public:

      typedef boost::function<void (const boost::system::error_code& error)> write_callback;

      ostream(fs& _fs, size_type size, size_type chunks)
      : fs_(_fs),
        ptr_(_fs.tail_++, 0),
        header_(size, chunks),
        bytes_written_(0)
      {
         leveldb::Slice skey(reinterpret_cast<const char*>(&ptr_), sizeof(chunkptr_type));
         std::string value(reinterpret_cast<const char *>(&header_), sizeof(header));
         if (fs_.chunks_->Put(leveldb::WriteOptions(), skey, value).ok())
            ++ptr_.second;
      }

      key_type key() const
      {
         return ptr_.first;
      }

      void write(const std::string& value, const write_callback& cb)
      {
         // if ptr hasn't been aligned to the first data chunk, it means we couldn't write the metadata
         boost::system::error_code e;
         if (!ptr_.second)
            e = boost::system::error_code(boost::system::errc::io_error, boost::system::system_category());
         else if (ptr_.second > header_.chunks)
            e = boost::asio::error::eof; // can't write past the allocated chunks
         else if (bytes_written_ + value.size() > header_.size) // can't write more bytes than we allocated
            e = boost::system::error_code(boost::system::errc::io_error, boost::system::system_category());
         else
         {
            leveldb::Slice skey(reinterpret_cast<const char*>(&ptr_), sizeof(chunkptr_type));
            if (!fs_.chunks_->Put(leveldb::WriteOptions(), skey, value).ok())
               e = boost::system::error_code(boost::system::errc::io_error, boost::system::system_category());
            else
            {
               ++ptr_.second;
               bytes_written_ += value.size();
            }
         }
         cb(e);
      }

      size_type size() const
      {
         return header_.size;
      }

   private:

      fs& fs_;
      chunkptr_type ptr_;
      header header_;
      size_type bytes_written_;
   };

private:

   // compare keys as native uint64's instead of lexically
   class comparator : public leveldb::Comparator
   {
   public:
      int Compare(const leveldb::Slice& a, const leveldb::Slice& b) const
      {
         chunkptr_type uia = *reinterpret_cast<const chunkptr_type*>(a.data());
         chunkptr_type uib = *reinterpret_cast<const chunkptr_type*>(b.data());
         return (uia < uib ? -1 : (uia > uib ? 1 : 0));
      }
      const char* Name() const { return "fs::comparator"; }
      void FindShortestSeparator(std::string*, const leveldb::Slice&) const { }
      void FindShortSuccessor(std::string*) const { }
   };

   boost::scoped_ptr<leveldb::DB> chunks_;
   boost::scoped_ptr<comparator> cmp_;

   key_type tail_;

   boost::asio::io_service& ios_;
};

} // darner

#endif // __DARNER_FS_HPP__
