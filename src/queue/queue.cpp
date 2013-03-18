#include "darner/queue/queue.h"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>

#include <leveldb/iterator.h>
#include <leveldb/write_batch.h>

#include "darner/util/log.h"

using namespace std;
using namespace boost;
using namespace darner;

queue::queue(asio::io_service& ios, const string& path)
: cmp_(new comparator()),
  queue_head_(key_type::KT_QUEUE, 0),
  queue_tail_(key_type::KT_QUEUE, 0),
  chunks_head_(key_type::KT_CHUNK, 0),
  items_open_(0),
  bytes_evicted_(0),
  destroy_(false),
  wake_up_it_(waiters_.begin()),
  ios_(ios),
  path_(path)
{
   leveldb::Options options;
   options.create_if_missing = true;
   options.comparator = cmp_.get();
   leveldb::DB* pdb;
   if (!leveldb::DB::Open(options, path, &pdb).ok())
      throw runtime_error("can't open journal: " + path);
   journal_.reset(pdb);
   // get head and tail of queue
   scoped_ptr<leveldb::Iterator> it(journal_->NewIterator(leveldb::ReadOptions()));
   it->Seek(key_type(key_type::KT_QUEUE, 0).slice());
   if (it->Valid())
   {
      queue_tail_ = key_type(it->key());
      it->Seek(key_type(key_type::KT_CHUNK, 0).slice());
      if (!it->Valid())
      {
         it->SeekToLast();
         queue_head_.id = key_type(it->key()).id + 1;
      }
      else // we have chunks!  so fetch chunks head too
      {
         it->Prev();
         queue_head_.id = key_type(it->key()).id + 1;
         it->SeekToLast();
         chunks_head_.id = key_type(it->key()).id + 1;
      }
   }
}

queue::~queue()
{
   journal_.reset();
   // TODO: most non-crap filesystems should be able to drop large files quickly, but this will block painfully on ext3.
   // one ugly solution is a separate delete thread.  or we can wait out everyone upgrading to ext4    :)
   if (destroy_)
      boost::filesystem::remove_all(path_);
}

void queue::wait(size_type wait_ms, const wait_callback& cb)
{
   ptr_list<waiter>::iterator it = waiters_.insert(waiters_.end(), new waiter(ios_, wait_ms, cb));
   if (wake_up_it_ == waiters_.end())
      wake_up_it_ = it;
   it->timer.async_wait(bind(&queue::waiter_wakeup, shared_from_this(), asio::placeholders::error, it));
}

void queue::destroy()
{
   if (destroy_)
      return; // already going to delete on dtor!

   // rename the journal dir in case the user creates a new queue with the same name before this one is destroyed
   string new_path = path_ + ".0";
   for (size_t i = 0; boost::filesystem::exists(new_path); ++i)
      new_path = path_ + "." + lexical_cast<string>(i);
   journal_.reset();
   boost::filesystem::rename(path_, new_path);

   leveldb::DB* pdb;
   leveldb::Options options;
   options.comparator = cmp_.get();
   if (!leveldb::DB::Open(options, new_path, &pdb).ok())
      throw runtime_error("can't open journal: " + path_); // should never happen, but fatal if it does

   journal_.reset(pdb);
   path_ = new_path;
   destroy_ = true;
}

queue::size_type queue::count() const
{
   return (queue_head_.id - queue_tail_.id) + returned_.size();
}

void queue::write_stats(const string& name, ostringstream& out) const
{
   out << "STAT queue_" << name << "_items " << count() << "\r\n";
   out << "STAT queue_" << name << "_waiters " << waiters_.size() << "\r\n";
   out << "STAT queue_" << name << "_open_transactions " << items_open_ << "\r\n";
}

// protected:

void queue::push(id_type& result, const string& item, bool sync)
{
   // items that end in 0 are escaped to (0, 0), so we can distinguish them from headers (which end in (1, 0))
   if (item[item.size() - 1] == '\0')
      put(queue_head_, item + '\0', sync);
   else
      put(queue_head_, item, sync);

   result = queue_head_.id++;

   wake_up(); // in case there's a waiter waiting for this new item
}

void queue::push(id_type& result, const header_type& header, bool sync)
{
   std::string buf;

   header.str(buf);

   put(queue_head_.slice(), buf, sync);

   result = queue_head_.id++;

   wake_up(); // in case there's a waiter waiting for this new item
}

bool queue::pop_begin(id_type& result)
{
   if (!returned_.empty())
   {
      result = *returned_.begin();
      returned_.erase(returned_.begin());
   }
   else if (queue_tail_.id != queue_head_.id)
      result = queue_tail_.id++;
   else
      return false;

   ++items_open_;

   return true;
}

void queue::pop_read(std::string& result_item, header_type& result_header, id_type id)
{
   get(key_type(key_type::KT_QUEUE, id), result_item);

   result_header = header_type();

   // check the escapes
   if (result_item.size() > 2 && result_item[result_item.size() - 1] == '\0')
   {
      if (result_item[result_item.size() - 2] == '\1') // \1 \0 means header
         result_header = header_type(result_item);
      else if (result_item[result_item.size() - 2] == '\0') // \0 \0 means escaped \0
         result_item.resize(result_item.size() - 1);
      else
         throw system::system_error(system::errc::io_error,
            boost::asio::error::get_system_category()); // anything else is bad data
   }
}

void queue::pop_end(bool erase, id_type id, const header_type& header)
{
   if (erase)
   {
      leveldb::WriteBatch batch;
      batch.Delete(key_type(key_type::KT_QUEUE, id).slice());

      if (header.end > 1) // multi-chunk?
      {
         for (key_type k(key_type::KT_CHUNK, header.beg); k.id != header.end; ++k.id)
            batch.Delete(k.slice());
      }

      write(batch);

      bytes_evicted_ += header.size;

      // leveldb is conservative about reclaiming deleted keys from its underlying journal.  let's amortize this
      // reclamation cost by compacting the evicted range when it reaches 32MB in size.  note that this size may be
      // different than what's on disk, because of snappy compression
      if (bytes_evicted_ > 33554432)
      {
         compact();
         bytes_evicted_ = 0;
      }
   }
   else
   {
      returned_.insert(id);

      wake_up(); // in case there's a waiter waiting for this returned key
   }

   --items_open_;
}

void queue::reserve_chunks(header_type& result, size_type count)
{
   result = header_type(chunks_head_.id, chunks_head_.id + count, 0);
   chunks_head_.id += count;
}

void queue::write_chunk(const string& chunk, id_type chunk_key)
{
   put(key_type(key_type::KT_CHUNK, chunk_key), chunk);
}

void queue::read_chunk(string& result, id_type chunk_key)
{
   get(key_type(key_type::KT_CHUNK, chunk_key), result);
}

void queue::erase_chunks(const header_type& header)
{
   leveldb::WriteBatch batch;

   for (key_type k(key_type::KT_CHUNK, header.beg); k.id != header.end; ++k.id)
      batch.Delete(k.slice());

   write(batch);
}

// private:

void queue::wake_up()
{
   if (wake_up_it_ != waiters_.end())
   {
      wake_up_it_->timer.cancel();
      ++wake_up_it_;
   }
}

void queue::waiter_wakeup(const system::error_code& e, ptr_list<queue::waiter>::iterator waiter_it)
{
   if (waiter_it == wake_up_it_) // did we time out on the next waiter in line?  too bad.
      ++wake_up_it_;

   ptr_list<waiter>::auto_type waiter = waiters_.release(waiter_it);

   if (!returned_.empty() || queue_tail_.id < queue_head_.id)
      waiter->cb(system::error_code());
   else
      waiter->cb(asio::error::timed_out);
}

void queue::compact()
{
   scoped_ptr<leveldb::Iterator> it(journal_->NewIterator(leveldb::ReadOptions()));

   // compact queue range first
   key_type kq_beg(key_type::KT_QUEUE, 0);
   it->Seek(kq_beg.slice());
   if (!it->Valid())
      return;

   key_type kq_end(it->key());
   --kq_end.id; // leveldb::CompactRange is inclusive [beg, end]
   leveldb::Slice sq_beg = kq_beg.slice(), sq_end = kq_end.slice();
   journal_->CompactRange(&sq_beg, &sq_end);

   // now compact chunk range
   key_type kc_beg(key_type::KT_CHUNK, 0);
   it->Seek(kc_beg.slice());
   if (!it->Valid())
      return log::INFO("queue<%1%>: compacted queue range to %2%", path_, kq_end.id);

   key_type kc_end(it->key());
   --kc_end.id;
   leveldb::Slice sc_beg = kc_beg.slice(), sc_end = kc_end.slice();
   journal_->CompactRange(&sc_beg, &sc_end);
   log::INFO("queue<%1%>: compacted queue range to %2%, chunk range to %3%", path_, kq_end.id, kc_end.id);
}

// child classes
void queue::header_type::str(std::string& out) const
{
   out = string(reinterpret_cast<const char *>(this), sizeof(header_type)) + '\1' + '\0';
}

leveldb::Slice queue::key_type::slice() const
{
   *reinterpret_cast<id_type*>(&buf_[0]) = id;
   buf_[sizeof(id_type)] = type;
   return leveldb::Slice(&buf_[0], sizeof(buf_));
}

