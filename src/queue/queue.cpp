#include "darner/queue/queue.h"

#include <boost/bind.hpp>

#include <leveldb/iterator.h>
#include <leveldb/write_batch.h>

using namespace std;
using namespace boost;
using namespace darner;

queue::queue(asio::io_service& ios, const string& path)
: cmp_(new comparator()),
  queue_head_(key_type::KT_QUEUE, 0),
  queue_tail_(key_type::KT_QUEUE, 0),
  chunks_head_(key_type::KT_CHUNK, 0),
  items_open_(0),
  wake_up_it_(waiters_.begin()),
  ios_(ios)
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

void queue::wait(size_type wait_ms, const wait_callback& cb)
{
   ptr_list<waiter>::iterator it = waiters_.insert(waiters_.end(), new waiter(ios_, wait_ms, cb));
   if (wake_up_it_ == waiters_.end())
      wake_up_it_ = it;
   it->timer.async_wait(bind(&queue::waiter_wakeup, this, asio::placeholders::error, it));
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

void queue::push(id_type& result, const string& item)
{
   // items that end in 0 are escaped to (0, 0), so we can distinguish them from headers (which end in (1, 0))
   if (item[item.size() - 1] == '\0')
      put(queue_head_, item + '\0');
   else
      put(queue_head_, item);

   result = queue_head_.id++;

   wake_up(); // in case there's a waiter waiting for this new item
}

void queue::push(id_type& result, const header_type& header)
{
   put(queue_head_.slice(), header.str());

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
         throw system::system_error(system::errc::io_error, system::system_category()); // anything else is bad data
   }

   ++items_open_;
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

      // leveldb is conservative about reclaiming deleted keys, of which there will be many when a queue grows and
      // later shrinks.  let's explicitly force it to compact every 1024 deletes
      if (id % 1024 == 0)
         journal_->CompactRange(NULL, NULL);
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

// child classes
const string& queue::header_type::str() const
{
   buf_ = string(reinterpret_cast<const char *>(this), sizeof(header_type)) + '\1' + '\0';
   return buf_;
}

leveldb::Slice queue::key_type::slice() const
{
   *reinterpret_cast<id_type*>(&buf_[0]) = id;
   buf_[sizeof(id_type)] = type;
   return leveldb::Slice(&buf_[0], sizeof(buf_));
}

int queue::key_type::compare(const key_type& other) const
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

