#pragma once
#include "common/udp.hpp"
#include "common/tcp.hpp"
#include "common/logger.hpp"
#include <poll.h>
#include <unistd.h>

namespace s2m
{
   uint16_t SERVE_UDP_PORT = 12121;
   uint16_t SERVE_TCP_PORT = 12121;

struct client_t
{
   client_t(std::string const & host)
      : host_(host)
   {
      udp_sock_.connect(host, SERVE_UDP_PORT);
      udp_sock_.join_group(true);
      udp_sock_.set_echo(true);
      udp_sock_.bind();
   }

   void run()
   {
      while(true)
      {
         do_stuff();
         sleep(1);
      }
   }

   struct hash_struct
   {
      int hash;
   };

   struct data_t
   {
      data_t(bool read)
         : ready(false)
         , offset(0)
         , len_offs(0)
         , read_str(read)
      {
      }

      bool read_non_block(tcp::socket_t & sock)
      {
         if(ready)
            return true;
         assert(read_str);
         pollfd fd;
         fd.fd = *sock;
         fd.events = POLLIN;
         int res = ::poll(&fd, 1, 0);
         if(res == -1)
            throw tcp::net_error(std::string("Poll failed: ") + strerror(errno));
         if(res == 0)
            return false;
         if(!len && (fd.revents & POLLIN))
         {
            size_t cnt = sock.read(&tmp_len, sizeof(tmp_len) - len_offs, len_offs);
            len_offs += cnt;
            if(len_offs != sizeof(tmp_len))
               return false;
            len = tmp_len;
            if(!tmp_len > 0)
               return true;
            data.resize(tmp_len);
            int res = ::poll(&fd, 1, 0);
            if(res == -1)
               throw tcp::net_error(std::string("Poll failed: ") + strerror(errno));
            if(res == 0)
               return false;
         }
         if(len && (fd.revents & POLLIN))
         {
            size_t cnt = sock.read(&data[0], sizeof(*len) - offset, offset);
            offset += cnt;
            ready = (offset == (size_t)*len);
            return ready;
         }
      }

      bool write_non_block(tcp::socket_t & sock)
      {
         if(ready)
            return true;
         assert(!read_str);
         pollfd fd;
         fd.fd = *sock;
         fd.events = POLLOUT;
         int res = ::poll(&fd, 1, 0);
         if(res == -1)
            throw tcp::net_error(std::string("Poll failed: ") + strerror(errno));
         if(res == 0)
            return false;
         if(!len && (fd.revents & POLLOUT))
         {
            tmp_len = data.size();
            size_t cnt = sock.write(&tmp_len, sizeof(tmp_len) - len_offs, len_offs);
            len_offs += cnt;
            if(len_offs != sizeof(tmp_len))
               return false;
            len = tmp_len;
            int res = ::poll(&fd, 1, 0);
            if(res == -1)
               throw tcp::net_error(std::string("Poll failed: ") + strerror(errno));
            if(res == 0)
               return false;
         }
         if(len && (fd.revents & POLLOUT))
         {
            size_t cnt = sock.write(&data[0], data.size() - offset, offset);
            offset += cnt;
            ready = (offset == data.size());
            return ready;
         }
      }

      std::vector<char> data;
      bool ready;
   private:
      boost::optional<int> len;
      int tmp_len;
      size_t offset;
      size_t len_offs;
      bool read_str;
   };

   void sync_data()
   {
      assert(tcp_sock_);
      if(read_struct_->read_non_block(*tcp_sock_) && write_struct_->write_non_block(*tcp_sock_))
      {
         read_struct_.reset();
         write_struct_.reset();
         tcp_sock_.reset();
      }
   }

   void sendhash()
   {
      hash_struct h;
      h.hash = 0;

      udp_sock_.send(&h, 1);
      logger::debug() << "Hash sended " << h.hash;
   }

   void recvhash()
   {
      hash_struct h[10];

      size_t n = udp_sock_.recv(h, 10);
      assert(n % sizeof(hash_struct) == 0);
      n /= sizeof(hash_struct);
      logger::debug() << "Hash recieved(" << n << "): " << h[0].hash;
   }

   void do_stuff()
   {
      pollfd fd;
      fd.fd = *udp_sock_;
      fd.events = POLLIN | POLLOUT;
      int res = ::poll(&fd, 1, 0);
      if(res < 0)
         throw std::runtime_error(std::string("Poll failed: ") + strerror(errno));
      if(res == 0)
         return;
      if(fd.revents & POLLOUT)
         sendhash();
      if(fd.revents & POLLIN)
         recvhash();
   }
private:
   udp::socket_t udp_sock_;
   tcp::socket_t tcp_server_sock_;
   boost::optional<tcp::socket_t> tcp_sock_;
   boost::optional<data_t> read_struct_, write_struct_;
   std::string host_;
};

}
