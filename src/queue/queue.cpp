#include "darner/queue/queue.h"

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

// protected:

bool queue::push(optional<id_type>& result, const string& value)
{
   // values that end in 0 are escaped to (0, 0), so we can distinguish them from headers (which end in (1, 0))
   if (value[value.size() - 1] == '\0')
   {
      string new_value = value + '\0';
      if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), new_value).ok())
         return false;
   }
   else if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), value).ok())
      return false;
   result = queue_head_.id++;

   spin_waiters(); // in case there's a waiter waiting for this new item

   return true;
}

bool queue::push(optional<id_type>& result, const header_type& value)
{
   if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), value.str()).ok())
      return false;

   result = queue_head_.id++;

   spin_waiters(); // in case there's a waiter waiting for this new item

   return true;
}

void queue::pop_open(optional<id_type>& result_id, optional<header_type>& result_header, string& result_value,
      size_type wait_ms, const success_callback& cb)
{
   if (next_id(result_id)) // is there an item ready to go?
   {
      if (!get_item(*result_id, result_header, result_value))
         return cb(system::error_code(system::errc::io_error, system::system_category()));
      return cb(system::error_code());
   }
   else if (wait_ms) // can we at least wait a bit?
   {
      ptr_list<waiter>::iterator it = waiters_.insert(waiters_.end(), new waiter(ios_, result_id, result_header,
         result_value, wait_ms, cb));
      it->timer.async_wait(bind(&queue::waiter_timeout, this, asio::placeholders::error, it));      
   }
   else
      return cb(asio::error::not_found); // can't wait?  no item for you
}

bool queue::pop_close(bool remove, id_type id, const optional<header_type>& header)
{
   if (remove)
   {
      leveldb::WriteBatch batch;
      batch.Delete(key_type(key_type::KT_QUEUE, id).slice());
      if (header)
         for (key_type k(key_type::KT_CHUNK, header->beg); k.id != header->end; ++k.id)
            batch.Delete(k.slice());
      return journal_->Write(leveldb::WriteOptions(), &batch).ok();
   }
   else
   {
      returned_.insert(id);

      // in case there's a waiter waiting for this returned key
      spin_waiters();

      return true;
   }
}

queue::size_type queue::count()
{
   return (queue_head_.id - queue_tail_.id) + returned_.size();
}

void queue::reserve_chunks(optional<header_type>& result, size_type chunks)
{
   result = header_type(chunks_head_.id, chunks_head_.id + chunks, 0);
   chunks_head_.id += chunks;
}

bool queue::write_chunk(const string& value, id_type chunk_key)
{
   key_type key(key_type::KT_CHUNK, chunk_key);
   return journal_->Put(leveldb::WriteOptions(), key.slice(), value).ok();
}

bool queue::read_chunk(string& result, id_type chunk_key)
{
   key_type key(key_type::KT_CHUNK, chunk_key);
   return journal_->Get(leveldb::ReadOptions(), key.slice(), &result).ok();
}

bool queue::erase_chunks(const header_type& header)
{
   leveldb::WriteBatch batch;
   for (key_type k(key_type::KT_CHUNK, header.beg); k.id != header.end; ++k.id)
      batch.Delete(k.slice());
   return journal_->Write(leveldb::WriteOptions(), &batch).ok();
}

// private:

void queue::spin_waiters()
{
   while (true)
   {
      if (waiters_.empty())
         break;
      optional<id_type> result_id;
      if (!next_id(result_id))
         break;
      ptr_list<waiter>::auto_type waiter = waiters_.release(waiters_.begin());
      waiter->timer.cancel();
      waiter->result_id = result_id;
      if (!get_item(*waiter->result_id, waiter->result_header, waiter->result_value))
         return waiter->cb(system::error_code(system::errc::io_error, system::system_category()));
      waiter->cb(system::error_code());
   }
}

bool queue::next_id(optional<id_type> & id)
{
   if (!returned_.empty())
   {
      id = *returned_.begin();
      returned_.erase(returned_.begin());
   }
   else if (queue_tail_.id != queue_head_.id)
      id = queue_tail_.id++;
   else
      return false;
   return true;
}

bool queue::get_item(id_type result_id, optional<header_type>& result_header, string& result_value)
{
   if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_QUEUE, result_id).slice(), &result_value).ok())
      return false;

   // check the escapes
   if (result_value.size() > 2 && result_value[result_value.size() - 2] == '\1' &&
      result_value[result_value.size() - 1] == '\0')
   {
      result_header = header_type(result_value);
      return journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_CHUNK, result_header->beg).slice(),
         &result_value).ok();
   }
   else if (result_value.size() > 2 && result_value[result_value.size() - 1] == '\0' && result_value[result_value.size() - 2] == '\0')
      result_value.resize(result_value.size() - 1);

   return true;
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
   buf_.resize(sizeof(id_type) + 1);
   *reinterpret_cast<id_type*>(&buf_[0]) = id;
   buf_[sizeof(id_type)] = type;
   return leveldb::Slice(&buf_[0], buf_.size());
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

