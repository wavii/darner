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

void queue::wait(size_type wait_ms, const success_callback& cb)
{
   ptr_list<waiter>::iterator it = waiters_.insert(waiters_.end(), new waiter(ios_, wait_ms, cb));
   it->timer.async_wait(bind(&queue::waiter_timeout, this, asio::placeholders::error, it));
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

void queue::push(optional<id_type>& result, const string& value)
{
   // values that end in 0 are escaped to (0, 0), so we can distinguish them from headers (which end in (1, 0))
   if (value[value.size() - 1] == '\0')
   {
      string new_value = value + '\0';
      if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), new_value).ok())
         throw system::system_error(system::errc::io_error, system::system_category());
   }
   else if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), value).ok())
      throw system::system_error(system::errc::io_error, system::system_category());

   result = queue_head_.id++;

   spin_waiters(); // in case there's a waiter waiting for this new item
}

void queue::push(optional<id_type>& result, const header_type& value)
{
   if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), value.str()).ok())
      throw system::system_error(system::errc::io_error, system::system_category());

   result = queue_head_.id++;

   spin_waiters(); // in case there's a waiter waiting for this new item
}

bool queue::pop_open(optional<id_type>& result_id, optional<header_type>& result_header, string& result_value)
{
   if (!returned_.empty())
   {
      result_id = *returned_.begin();
      returned_.erase(returned_.begin());
   }
   else if (queue_tail_.id != queue_head_.id)
      result_id = queue_tail_.id++;
   else
      return false;

   if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_QUEUE, *result_id).slice(), &result_value).ok())
      throw system::system_error(system::errc::io_error, system::system_category());

   // check the escapes
   if (result_value.size() > 2 && result_value[result_value.size() - 1] == '\0')
   {
      if (result_value[result_value.size() - 2] == '\1') // \1 \0 means header
      {
         result_header = header_type(result_value);
         if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_CHUNK, result_header->beg).slice(),
            &result_value).ok())
            throw system::system_error(system::errc::io_error, system::system_category());
      }
      else if (result_value[result_value.size() - 2] == '\0') // \0 \0 means escaped \0
         result_value.resize(result_value.size() - 1);
      else
         throw system::system_error(system::errc::io_error, system::system_category()); // anything else is bad data
   }

   ++items_open_;

   return true;
}

void queue::pop_close(bool remove, id_type id, const optional<header_type>& header)
{
   if (remove)
   {
      leveldb::WriteBatch batch;
      batch.Delete(key_type(key_type::KT_QUEUE, id).slice());

      if (header)
         for (key_type k(key_type::KT_CHUNK, header->beg); k.id != header->end; ++k.id)
            batch.Delete(k.slice());

      if (!journal_->Write(leveldb::WriteOptions(), &batch).ok())
         throw system::system_error(system::errc::io_error, system::system_category());
   }
   else
   {
      returned_.insert(id);

      // in case there's a waiter waiting for this returned key
      spin_waiters();
   }

   --items_open_;
}

void queue::reserve_chunks(optional<header_type>& result, size_type chunks)
{
   result = header_type(chunks_head_.id, chunks_head_.id + chunks, 0);
   chunks_head_.id += chunks;
}

void queue::write_chunk(const string& value, id_type chunk_key)
{
   if (!journal_->Put(leveldb::WriteOptions(), key_type(key_type::KT_CHUNK, chunk_key).slice(), value).ok())
      throw system::system_error(system::errc::io_error, system::system_category());
}

void queue::read_chunk(string& result, id_type chunk_key)
{
   if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_CHUNK, chunk_key).slice(), &result).ok())
      throw system::system_error(system::errc::io_error, system::system_category());
}

void queue::erase_chunks(const header_type& header)
{
   leveldb::WriteBatch batch;

   for (key_type k(key_type::KT_CHUNK, header.beg); k.id != header.end; ++k.id)
      batch.Delete(k.slice());

   if (!journal_->Write(leveldb::WriteOptions(), &batch).ok())
      throw system::system_error(system::errc::io_error, system::system_category());
}

// private:

void queue::spin_waiters()
{
   while (true)
   {
      if (waiters_.empty() || (returned_.empty() && queue_tail_.id == queue_head_.id))
         break;
      ptr_list<waiter>::auto_type waiter = waiters_.release(waiters_.begin());
      waiter->timer.cancel();
      waiter->cb(system::error_code());
   }
}

void queue::waiter_timeout(const system::error_code& e, ptr_list<queue::waiter>::iterator waiter_it)
{
   if (e == asio::error::operation_aborted) // can be error if timer was canceled
      return;

   ptr_list<waiter>::auto_type waiter = waiters_.release(waiter_it);

   if (e) // weird unspecified error, better pass it up just in case
      waiter->cb(e);
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

