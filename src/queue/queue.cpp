#include "darner/queue/queue.h"

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

void queue::push(string& value, const push_callback& cb)
{
   // values that end in 0 are escaped to (0, 0), so we can distinguish them from headers (which end in (1, 0))
   if (value[value.size() - 1] == '\0')
      value.push_back('\0');

   file_type file;
   put_value(file, value, cb);
}

void queue::push_reserve(file_type& _return, size_type chunks)
{
   _return = file_type(0, header_type(chunks_head_.id, chunks_head_.id + chunks, 0), chunks_head_.id);
   chunks_head_.id += chunks;
}

void queue::push_chunk(file_type& file, string& value, const push_callback& cb)
{
   if (file.tell >= file.header.chunk_end)
      return cb(asio::error::eof, file);

   key_type key(key_type::KT_CHUNK, file.tell);
   if (!journal_->Put(leveldb::WriteOptions(), key.slice(), value).ok())
      return cb(system::error_code(system::errc::io_error, system::system_category()), file);

   file.header.size += value.size();

   if (++file.tell == file.header.chunk_end)
   {
      string qvalue = string(reinterpret_cast<const char *>(&file.header), sizeof(header_type)) + '\1' + '\0';
      put_value(file, qvalue, cb);
   }
   else
      cb(system::error_code(), file);
}

void queue::push_cancel(const file_type& file, const success_callback& cb)
{
   leveldb::WriteBatch batch;
   for (key_type k(key_type::KT_CHUNK, file.header.chunk_beg); k.id != file.header.chunk_end; ++k.id)
      batch.Delete(k.slice());
   if (!journal_->Write(leveldb::WriteOptions(), &batch).ok())
      cb(system::error_code(system::errc::io_error, system::system_category()));
   else
      cb(system::error_code());
}

/*
 * reserves an item for popping off the back of the queue.  calls cb after at most wait_ms milliseconds with a
 * success code, the item's key, and the item's value.  on failure, sets error as either timed_out if no
 * items were available after wait_ms milliseconds, or io_error if there was a problem with the underlying journal
 */
void queue::pop(size_t wait_ms, const pop_callback& cb)
{
   spin_waiters(); // first let's drive out any current waiters

   file_type file;
   if (!next_key(file)) // do we have an item right away?
   {
      if (wait_ms > 0) // okay, no item. can we fire up a timer and wait?
      {
         ptr_list<waiter>::iterator it = waiters_.insert(waiters_.end(), new waiter(ios_, wait_ms, cb));
         it->timer.async_wait(bind(&queue::waiter_timeout, this, asio::placeholders::error, it));
      }
      else
      {
         string nothing;
         cb(asio::error::not_found, file, nothing); // nothing?  okay, return back no item
      }
      return;
   }
   get_value(file, cb);
}

void queue::pop_chunk(file_type& file, const pop_callback& cb)
{
   string value;
   if (file.tell >= file.header.chunk_end)
      return cb(asio::error::eof, file, value);

   file.header = header_type(value);
   if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_CHUNK, file.tell).slice(), &value).ok())
      return cb(system::error_code(system::errc::io_error, system::system_category()), file, value);
   ++file.tell;

   cb(system::error_code(), file, value);
}

void queue::pop_end(const file_type& file, bool remove, const success_callback& cb)
{
   if (remove)
   {
      leveldb::WriteBatch batch;
      batch.Delete(key_type(key_type::KT_QUEUE, file.id).slice());
      for (key_type k(key_type::KT_CHUNK, file.header.chunk_beg); k.id != file.header.chunk_end; ++k.id)
         batch.Delete(k.slice());
      if (!journal_->Write(leveldb::WriteOptions(), &batch).ok())
         cb(system::error_code(system::errc::io_error, system::system_category()));
      else
         cb(system::error_code());
   }
   else
   {
      returned_.insert(file.id);
      cb(boost::system::error_code());

      // in case there's a waiter waiting for this returned key, let's fire a check_pop_
      spin_waiters();
   }
}

size_type queue::count()
{
   return (queue_head_.id - queue_tail_.id) + returned_.size();
}

// private:

void queue::spin_waiters()
{
   while (true)
   {
      if (waiters_.empty())
         break;
      file_type file;
      if (!next_key(file))
         break;
      ptr_list<waiter>::auto_type waiter = waiters_.release(waiters_.begin());
      waiter->timer.cancel();
      get_value(file, waiter->cb);
   }
}

bool queue::next_key(file_type & file)
{
   if (!returned_.empty())
   {
      file.id = *returned_.begin();
      returned_.erase(returned_.begin());
   }
   else if (queue_head_.id != queue_tail_.id)
   {
      file.id = queue_head_.id;
      ++queue_head_.id;
   }
   else
      return false;
   return true;
}

void queue::get_value(file_type& file, const pop_callback& cb)
{
   file.tell = 0;
   
   string value;
   if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_QUEUE, file.id).slice(), &value).ok())
      return cb(system::error_code(system::errc::io_error, system::system_category()), file, value);
   
   // check the escapes
   if (value.size() > sizeof(header_type) && value.size() > 2 && value[value.size() - 1] == '\1'
      && value[value.size() - 2] == '\0')
   {
      file.header = header_type(value);
      if (!journal_->Get(leveldb::ReadOptions(), key_type(key_type::KT_CHUNK, file.tell).slice(), &value).ok())
         return cb(system::error_code(system::errc::io_error, system::system_category()), file, value);
      ++file.tell;
   }
   else
   {
      if (value.size() > 2 && value[value.size() - 1] == '\0' && value[value.size() - 2] == '\0')
         value.resize(value.size() - 1);
      file.header = header_type(0, 0, value.size());
   }

   cb(system::error_code(), file, value);
}

void queue::put_value(file_type& file, const string& value, const push_callback& cb)
{
   if (!journal_->Put(leveldb::WriteOptions(), queue_head_.slice(), value).ok())
      return cb(system::error_code(system::errc::io_error, system::system_category()), file);

   file.id = queue_head_.id++; // post-increment head in case leveldb write fails
   cb(system::error_code(), file);

   spin_waiters(); // in case there's a waiter waiting for this
}

void queue::waiter_timeout(const system::error_code& e, ptr_list<queue::waiter>::iterator waiter_it)
{
   if (e == asio::error::operation_aborted) // can be error if timer was canceled
      return;

   ptr_list<waiter>::auto_type waiter = waiters_.release(waiter_it);

   string nothing;
   if (e) // weird unspecified error, better pass it up just in case
      waiter->cb(e, file_type(), nothing);
   else
      waiter->cb(asio::error::timed_out, file_type(), nothing);
}

