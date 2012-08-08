#include "darner/net/handler.h"

#include <sstream>
#include <boost/array.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace darner;

handler::handler(io_service& ios,
                 request_parser& parser,
                 queue_map& queues,
                 stats& _stats,
                 queue::size_type chunk_size /* = 4096 */)
   : chunk_size_(chunk_size),
     socket_(ios),
     parser_(parser),
     queues_(queues),
     stats_(_stats),
     in_(chunk_size + 2) // make room for \r\n
{
}

handler::~handler()
{
   mutex::scoped_lock lock(stats_.mutex);
   ++stats_.conns_closed;
}

void handler::start()
{
   {
      mutex::scoped_lock lock(stats_.mutex);
      ++stats_.conns_opened;
   }
   socket_.set_option(ip::tcp::no_delay(true));

   read_request(system::error_code(), 0);
}

void handler::read_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
      return log::ERROR("handler<%1%>::handle_read_request: %2%", shared_from_this(), e.message());

   async_read_until(
      socket_, in_, '\n',
      bind(&handler::parse_request, shared_from_this(), _1, _2));
}

void handler::parse_request(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      if (e != error::eof) // clean close by client
         log::ERROR("handler<%1%>::handle_read_request: %2%", shared_from_this(), e.message());
      return;
   }

   // TODO: it would have been nice to pass in an buffers_iterator directly to spirit, but
   // something constness thing about the iterator_traits::value_type is borking up being able to use it
   asio::streambuf::const_buffers_type bufs = in_.data();
   buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_transferred);
   if (!parser_.parse(req_, buf_))
      return write_result(false, "ERROR\r\n");
   in_.consume(bytes_transferred);

   switch (req_.type)
   {
   case request::RT_STATS:     write_stats();   break;
   case request::RT_VERSION:   write_version(); break;
   case request::RT_FLUSH:     flush();         break;
   case request::RT_FLUSH_ALL: flush_all();     break;
   case request::RT_SET:       set();           break;
   case request::RT_GET:       get();           break;
   }
}

void handler::write_stats()
{
   {
      mutex::scoped_lock lock(stats_.mutex);
      stats_.write(buf_);
   }
   async_write(socket_, buffer(buf_), bind(&handler::read_request, shared_from_this(), _1, _2));
}

void handler::write_version()
{
   write_result(true, "VERSION " + string(DARNER_VERSION) + "\r\n");
}

void handler::flush()
{
   // TODO: implement
}

void handler::flush_all()
{
   // TODO: implement
}

void handler::set()
{
   // round up the number of chunks we need, and fetch \r\n if it's just one chunk
   push_stream_ = in_place(ref(queues_[req_.queue]), (req_.num_bytes + chunk_size_ - 1) / chunk_size_);
   queue::size_type remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;

   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::set_on_read_chunk(const system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      queues_.get_io_service().post(bind(
         &oqstream::cancel, push_stream_, oqstream::success_callback(bind(
            &handler::do_nothing, shared_from_this(), _1))));

      return log::ERROR("handler<%1%>::set_on_read_chunk: %2%", shared_from_this(), e.message());
   }

   asio::streambuf::const_buffers_type bufs = in_.data();
   queue::size_type bytes_remaining = req_.num_bytes - push_stream_->tell();

   if (bytes_remaining <= chunk_size_) // last chunk!  make sure it ends with \r\n
   {
      buf_.assign(buffers_begin(bufs) + bytes_remaining, buffers_begin(bufs) + bytes_remaining + 2);
      if (buf_ != "\r\n")
         return write_result(false, "CLIENT_ERROR bad data chunk\r\n");
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + bytes_remaining);
      in_.consume(bytes_remaining + 2);
   }
   else
   {
      buf_.assign(buffers_begin(bufs), buffers_begin(bufs) + chunk_size_);
      in_.consume(chunk_size_);
   }

   queues_.get_io_service().post(bind(
      &oqstream::write, &*push_stream_, cref(buf_), socket_.get_io_service().wrap(bind(
         &handler::set_on_write_chunk, shared_from_this(), _1))));
}

void handler::set_on_write_chunk(const boost::system::error_code& e)
{
   if (e)
   {
      queues_.get_io_service().post(bind(
         &oqstream::cancel, push_stream_, oqstream::success_callback(bind(
            &handler::do_nothing, shared_from_this(), _1))));

      log::ERROR("handler<%1%>::set_on_write_chunk: %2%", shared_from_this(), e.message());
      return write_result(false, "SERVER_ERROR " + e.message() + "\r\n");
   }

   if (push_stream_->tell() == req_.num_bytes) // all done!
      return write_result(true, "STORED\r\n");

   // second verse, same as the first
   queue::size_type remaining = req_.num_bytes - push_stream_->tell();
   queue::size_type required = remaining > chunk_size_ ? chunk_size_ : remaining + 2;
   async_read(
      socket_, in_, transfer_at_least(required > in_.size() ? required - in_.size() : 0),
      bind(&handler::set_on_read_chunk, shared_from_this(), _1, _2));
}

void handler::get()
{
   if (req_.get_abort && (req_.get_open || req_.get_close))
      return write_result(false, "CLIENT_ERROR abort must be by itself\r\n");

   if (pop_stream_ && !req_.get_close && !req_.get_abort)
      return write_result(false, "CLIENT_ERROR close current item first\r\n");

   if (!pop_stream_)
   {
      pop_stream_ = in_place(ref(queues_[req_.queue]), req_.wait_ms);
      queues_.get_io_service().post(
         bind(&iqstream::read, pop_stream_, ref(buf_),
            socket_.get_io_service().wrap(bind(&handler::get_on_read_first_chunk, shared_from_this(), _1))));
   }
   else // before getting the next item, close or abort this one out first
      queues_.get_io_service().post(
         bind(&iqstream::close, pop_stream_, req_.get_close,
            socket_.get_io_service().wrap(bind(&handler::get_on_pop_close_pre, shared_from_this(), _1))));
}

void handler::get_on_pop_close_pre(const boost::system::error_code& e)
{
   if (e)
   {
      log::ERROR("handler<%1%>::get_on_pop_close_pre: %2%", shared_from_this(), e.message());
      return write_result(false, "SERVER_ERROR " + e.message() + "\r\n");
   }

   if (req_.get_abort)
      return write_result(true, "END\r\n"); // aborts go no further

   pop_stream_ = in_place(ref(queues_[req_.queue]), req_.wait_ms);
   queues_.get_io_service().post(
      bind(&iqstream::read, pop_stream_, ref(buf_),
         socket_.get_io_service().wrap(bind(&handler::get_on_read_first_chunk, shared_from_this(), _1))));
}

void handler::get_on_read_first_chunk(const boost::system::error_code& e)
{
   if (e == asio::error::timed_out || e == asio::error::not_found)
      return write_result(true, "END\r\n");
   else if (e)
   {
      log::ERROR("handler<%1%>::get_on_read_first_chunk: %2%", shared_from_this(), e.message());
      return write_result(false, "SERVER_ERROR " + e.message() + "\r\n");
   }

   ostringstream oss;
   oss << "VALUE " << req_.queue << " 0 " << pop_stream_->size() << "\r\n";
   header_buf_ = oss.str();
   array<const_buffer, 2> bufs = {{ buffer(header_buf_), buffer(buf_) }};

   async_write(socket_, bufs, bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
}

void handler::get_on_read_next_chunk(const boost::system::error_code& e)
{
   if (e)
   {
      log::ERROR("handler<%1%>::get_on_read_next_chunk: %2%", shared_from_this(), e.message());
      return write_result(false, "");
   }

   async_write(socket_, buffer(buf_), bind(&handler::get_on_write_chunk, shared_from_this(), _1, _2));
}

void handler::get_on_write_chunk(const boost::system::error_code& e, size_t bytes_transferred)
{
   if (e)
   {
      log::ERROR("handler<%1%>::get_on_write_chunk: %2%", shared_from_this(), e.message());
      return write_result(false, "");
   }

   if (pop_stream_->tell() == pop_stream_->size())
   {
      if (req_.get_open)
         write_result(true, "\r\nEND\r\n");
      else
         queues_.get_io_service().post(
            bind(&iqstream::close, pop_stream_, true,
               socket_.get_io_service().wrap(bind(&handler::get_on_pop_close_post, shared_from_this(), _1))));
   }
   else
      queues_.get_io_service().post(
         bind(&iqstream::read, pop_stream_, ref(buf_),
            socket_.get_io_service().wrap(bind(&handler::get_on_read_next_chunk, shared_from_this(), _1))));
}

void handler::get_on_pop_close_post(const boost::system::error_code& e)
{
   if (e)
      log::ERROR("handler<%1%>::get_on_pop_close_pre: %2%", shared_from_this(), e.message());
   else
      write_result(true, "\r\nEND\r\n");
}

void handler::write_result(bool success, const std::string& msg)
{
   buf_ = msg;
   if (success)
      async_write(socket_, buffer(buf_), bind(&handler::read_request, shared_from_this(), _1, _2));
   else
      async_write(socket_, buffer(buf_), bind(&handler::do_nothing, shared_from_this(), _1, _2));
}

void handler::do_nothing(const system::error_code& e, size_t bytes_transferred)
{
}

void handler::do_nothing(const system::error_code& e)
{
}
