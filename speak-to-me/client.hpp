#pragma once
#include "common/udp.hpp"
#include "common/tcp.hpp"
#include "common/logger.hpp"
#include "common/stuff.hpp"
#include "streamer.hpp"

#include <poll.h>
#include <unistd.h>
#include <boost/function.hpp>
#include <boost/thread.hpp>
#include <ifaddrs.h>
#include <unordered_map>

namespace s2m
{
   uint16_t SERVE_UDP_PORT = 12121;
   uint16_t SERVE_TCP_PORT = 12121;

   uint32_t USER_TIMEOUT = 10; // user is dead if no activity for N secs
   uint32_t PROCESS_PERIOD = 3;

struct client_t
{
   client_t(std::string const & host, in_addr const & local_ip)
      : host_(host)
      , local_ip_(local_ip)
      , nick_("default")
      , input_device_(0)
      , output_device_(0)
      , api_(0)
   {
      udp_sock_.connect(host, SERVE_UDP_PORT);
//      udp_sock_.set_broadcast(true);
      udp_sock_.join_group(true);
      udp_sock_.set_echo(false);
      udp_sock_.bind();

      tcp_server_sock_.bind(SERVE_TCP_PORT);
      tcp_server_sock_.listen();

      users_.insert(std::make_pair(local_ip_, user_t(local_ip_, nick_)));
      stuff_hash_ = compute_hash();
   }

   typedef
      boost::unique_lock<boost::recursive_mutex>
      lock_t;

   struct user_t
   {
      user_t()
         : room_port(0)
         , timestamp(0)
      {
         util::nullize(room_address);
         util::nullize(ip);
      }
      user_t(in_addr const & ip, std::string const& nick)
         : ip(ip)
         , nick(nick)
         , room_port(0)
         , timestamp(0)
      {
         util::nullize(room_address);
      }

      in_addr ip;
      std::string nick;
      in_addr room_address;
      uint16_t room_port;
      uint32_t timestamp;
   };

   typedef
      std::unordered_map<in_addr, user_t, util::hasher<in_addr>>
      users_map_t;

   void set_nick(std::string const & nick)
   {
      lock_t __(users_mutex_);

      nick_ = nick;
      users_[local_ip_].nick = nick;
      stuff_hash_ = compute_hash();
   }

   void set_devices(int api, int inp, int outp)
   {
      api_ = api;
      input_device_ = inp;
      output_device_ = outp;
   }

   bool has_room() const
   {
      return !!streamer_;
   }

   void disconnect()
   {
      streamer_.reset();
      {
         lock_t __(users_mutex_);

         auto & me = users_[local_ip_];
         me.room_port = 0;
         util::nullize(me.room_address);
         stuff_hash_ = compute_hash();
      }
   }

   void set_room(in_addr const & addr, uint16_t port)
   {
      {
         lock_t __(users_mutex_);

         auto & me = users_[local_ip_];
         me.room_port = port;
         me.room_address = addr;
         stuff_hash_ = compute_hash();
      }
      streamer_ = boost::in_place(addr, port);
      streamer_->init(api_);
      streamer_->run(input_device_, output_device_);
   }

   void remove_dead_users()
   {
      lock_t __(users_mutex_);
      uint32_t cur_time = ::time(NULL);
      bool removed = false;
      for(auto it = users_.begin(); it != users_.end(); )
         if(!(it->first == local_ip_) && it->second.timestamp + USER_TIMEOUT < cur_time)
         {
            it = users_.erase(it);
            removed = true;
         }
         else
            ++it;
      if(removed)
         stuff_hash_ = compute_hash();
   }

   void run()
   {
      while(true)
      {
         do_stuff();
         sleep(PROCESS_PERIOD);
      }
   }

#pragma pack(push, 1)
   struct hash_struct
   {
      in_addr ip;
      uint32_t hash;
   };
#pragma pack(pop)

   struct data_t
   {
      data_t(bool read)
         : ready(false)
         , offset(0)
         , len_offs(0)
         , read_str(read)
      {
      }

      void validate(int revents, std::string const & pref)
      {
         if(revents & POLLNVAL)
            throw tcp::net_error(pref + std::string("Poll failed: POLLNVAL"));
         if(revents & POLLHUP)
            throw tcp::net_error(pref + std::string("Poll failed: POLLHUP"));
         if(revents & POLLERR)
            throw tcp::net_error(pref + std::string("Poll failed: POLLERR ") + strerror(errno));
      }

      bool read_non_block(tcp::socket_t & sock)
      {
         if(ready)
            return true;
         assert(read_str);
         pollfd fd;
         fd.fd = *sock;
         fd.events = POLLIN;
         fd.revents = 0;
         int res = ::poll(&fd, 1, 0);
         if(res == -1)
            throw tcp::net_error(std::string("read_non_block: Poll failed: ") + strerror(errno));
         validate(fd.revents, "read_non_block: ");
         if(res == 0)
            return false;
         if(!len && (fd.revents & POLLIN))
         {
            size_t cnt = sock.read(&tmp_len, sizeof(tmp_len) - len_offs, len_offs);
            logger::trace() << "read_non_block: reading len len_offs = " << len_offs << " cnt = " << cnt;
            len_offs += cnt;
            if(len_offs != sizeof(tmp_len))
               return false;
            len = tmp_len;
            logger::trace() << "read_non_block: readed len = " << *len;
            if(!tmp_len > 0)
               return true;
            data.resize(tmp_len);
            fd.revents = 0;
            int res = ::poll(&fd, 1, 0);
            if(res == -1)
               throw tcp::net_error(std::string("read_non_block: Poll failed: ") + strerror(errno));
            if(res == 0)
               return false;
         }
         validate(fd.revents, "read_non_block: ");
         if(len && (fd.revents & POLLIN))
         {
            size_t cnt = sock.read(&data[0], *len - offset, offset);
            logger::trace() << "read_non_block: readed data offs = " << offset << " cnt = " << cnt;
            offset += cnt;
            ready = (offset == (size_t)*len);
            if(ready)
               logger::trace() << "read_non_block: ready";
         }
//         std::cin.ignore(1);
         return ready;
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
            throw tcp::net_error(std::string("write_non_block: Poll failed: ") + strerror(errno));
         if(res == 0)
            return false;
         validate(fd.revents, "write_non_block: ");

         if(!len && (fd.revents & POLLOUT))
         {
            tmp_len = data.size();
            logger::trace() << "write_non_block: writing len len_offs = " << len_offs;
            size_t cnt = sock.write(&tmp_len, sizeof(tmp_len) - len_offs, len_offs);
            len_offs += cnt;
            if(len_offs != sizeof(tmp_len))
               return false;
            len = tmp_len;
            logger::trace() << "write_non_block: written len = " << *len;
            int res = ::poll(&fd, 1, 0);
            if(res == -1)
               throw tcp::net_error(std::string("write_non_block: Poll failed: ") + strerror(errno));
            if(res == 0)
               return false;
         }
         validate(fd.revents, "write_non_block: ");
         if(len && (fd.revents & POLLOUT))
         {
//            sock.nodelay(true);
            size_t cnt = sock.write(&data[0], data.size() - offset, offset);
            logger::trace() << "write_non_block: written data offs = " << offset << " cnt = " << cnt;
            offset += cnt;
            ready = (offset == data.size());
            if(ready)
            {
               logger::trace() << "write_non_block: ready";
            }
//            sock.nodelay(false);
         }
         return ready;
      }

      std::vector<char> data;
      bool ready;
   private:
      boost::optional<int> len;
      int32_t tmp_len;
      uint32_t offset;
      uint32_t len_offs;
      bool read_str;
   };

   void sync_data()
   {
      assert(tcp_sock_);
      read_struct_->read_non_block(*tcp_sock_);
      write_struct_->write_non_block(*tcp_sock_);
      if(read_struct_->ready && write_struct_->ready)
      {
         std::vector<user_t> info;
         parse_list(read_struct_->data, info);
         {
            lock_t __(users_mutex_);
            for(user_t const & user : info)
            {
               auto it = users_.find(user.ip);
               if(it == users_.end())
               {
                  users_.insert(std::make_pair(user.ip, user));
                  continue;
               }
               if(it->second.timestamp > user.timestamp)
                  continue;
               it->second = user;
            }
            stuff_hash_ = compute_hash();
         }
         reset_tcp();
         logger::trace() << "sync_data: sync completed";
      }
   }

   void sendhash()
   {
      hash_struct h;
      h.hash = stuff_hash_;

      h.ip = local_ip_;

      udp_sock_.send(&h, 1);
      logger::debug() << "Hash sended " << h.hash;
   }

   void recvhash()
   {
      hash_struct h[10];

      size_t n = udp_sock_.recv(h, 10);
      assert(n % sizeof(hash_struct) == 0);
      n /= sizeof(hash_struct);
      logger::debug() << "Hash recieved(" << n << ") from " << inet_ntoa(h[0].ip) <<": " << h[0].hash;

      uint32_t cur_time = ::time(NULL);
      for(size_t i = 0; i < n; ++i)
         if(h[i].hash != stuff_hash_)
         {
            start_syncing(h[i].ip);
            break;
         }
         else
         {
            lock_t __(users_mutex_);
            auto it = users_.find(h[i].ip);
            if(it != users_.end())
               it->second.timestamp = cur_time;
         }
   }

   void reset_tcp()
   {
      logger::trace() << "client::reset_tcp";
      tcp_sock_.reset();
      read_struct_.reset();
      write_struct_.reset();
   }

   void start_syncing(in_addr const & ip)
   {
      if(tcp_sock_)
         return;
      try
      {
         logger::trace() << "client::start_syncing: syncing " << inet_ntoa(ip);
         tcp_sock_ = boost::in_place();
         assert(tcp_sock_);
         tcp_sock_->connect(ip, SERVE_TCP_PORT);
         tcp_addr_ = ip;
         read_struct_ = data_t(true);
         write_struct_ = data_t(false);
         generate_list(write_struct_->data);
      }
      catch(tcp::net_error & e)
      {
         logger::warning() << "client::start_syncing: " << e.what();
         reset_tcp();
      }
   }

   void generate_list(std::vector<char> & data) const
   {
      lock_t __(users_mutex_);
      uint32_t cur_time = ::time(NULL);
      auto it = std::back_inserter(data);
      for(auto const & user : users_)
      {
         it = std::copy(reinterpret_cast<const char*>(&user.second.ip), reinterpret_cast<const char*>(&user.second.ip) + sizeof(user.second.ip), it);
         *it = user.second.nick.size();
         ++it;
         it = std::copy(user.second.nick.begin(), user.second.nick.end(), it);
         uint32_t ts = user.second.timestamp - cur_time;
         if(user.second.ip == local_ip_)
            ts = 0;
         it = std::copy(reinterpret_cast<const char*>(&ts), reinterpret_cast<const char*>(&ts) + sizeof(ts), it);
         it = std::copy(reinterpret_cast<const char*>(&user.second.room_address), reinterpret_cast<const char*>(&user.second.room_address) + sizeof(user.second.room_address), it);
         it = std::copy(reinterpret_cast<const char*>(&user.second.room_port), reinterpret_cast<const char*>(&user.second.room_port) + sizeof(user.second.room_port), it);
      }
   }

   void parse_list(std::vector<char> const & data, std::vector<user_t> & res) const
   {
      size_t offset = 0;
      uint32_t cur_time = ::time(NULL);
      while(offset < data.size())
      {
         user_t user;
         if(offset + sizeof(user.ip) > data.size())
            throw tcp::net_error("invalid format");
         user.ip = *reinterpret_cast<const in_addr*>(&data[offset]);
         offset += sizeof(user.ip);

         if(offset + 1 > data.size())
            throw tcp::net_error("invalid format");
         size_t nlen = data[offset];
         offset += 1;

         if(offset + nlen > data.size())
            throw tcp::net_error("invalid format");
         user.nick = std::string(data.begin() + offset, data.begin() + offset + nlen);
         offset += nlen;

         if(offset + sizeof(user.timestamp) > data.size())
            throw tcp::net_error("invalid format");
         user.timestamp = *reinterpret_cast<const uint32_t*>(&data[offset]);
         user.timestamp += cur_time;
         offset += sizeof(user.timestamp);

         if(offset + sizeof(user.room_address) > data.size())
            throw tcp::net_error("invalid format");
         user.room_address = *reinterpret_cast<const in_addr*>(&data[offset]);
         offset += sizeof(user.room_address);

         if(offset + sizeof(user.room_port) > data.size())
            throw tcp::net_error("invalid format");
         user.room_port = *reinterpret_cast<const uint16_t*>(&data[offset]);
         offset += sizeof(user.room_port);

         if(!(user.ip == local_ip_))
            res.push_back(user);
      }
   }

   uint32_t compute_hash() const
   {
      lock_t __(users_mutex_);
      uint32_t res = 0;
      for(auto const & user : users_)
      {
         util::hash_combine(res, util::hash(user.second.ip));
         util::hash_combine(res, util::hash(user.second.nick));
         util::hash_combine(res, util::hash(user.second.room_address));
         util::hash_combine(res, util::hash(user.second.room_port));
      }
      logger::trace() << "client_t::compute_hash";
      return res;
   }

   void do_stuff()
   {
      pollfd fds[2];
      fds[0].fd = *udp_sock_;
      fds[0].events = POLLIN | POLLOUT;
      fds[1].fd = *tcp_server_sock_;
      fds[1].events = POLLIN;

      int res = ::poll(fds, 2, 0);
      if(res < 0)
         throw std::runtime_error(std::string("Poll failed: ") + strerror(errno));
      if(res == 0)
         return;
      if((fds[0].revents & POLLOUT) != 0 && !tcp_sock_)
         sendhash();
      if(fds[0].revents & POLLIN)
         recvhash();

      remove_dead_users();

      if((fds[1].revents & POLLIN))
      {
         sockaddr_in tmp;
         socklen_t tmp_len = sizeof(sockaddr_in);
         int res = ::accept(*tcp_server_sock_, (sockaddr*)&tmp, &tmp_len);
         if(res == -1)
//            throw tcp::net_error(std::string("Accept failed: ") + strerror(errno));
            logger::warning() << (std::string("Accept failed: ") + strerror(errno));
         else if(!tcp_sock_ || (tcp_addr_ == tmp.sin_addr && local_ip_ < tcp_addr_))
         {
            if(tcp_sock_)
            {
               logger::trace() << "client::do_stuff: already connected but i will be rejected";
               reset_tcp();
            }
            read_struct_ = data_t(true);
            write_struct_ = data_t(false);
            generate_list(write_struct_->data);
            tcp_sock_ = boost::in_place(res);
            tcp_addr_ = tmp.sin_addr;
            logger::trace() << "client::do_stuff: serving " << inet_ntoa(tmp.sin_addr);
         }
         else
         {
            ::close(res);
            //tcp::socket_t(res);//it is really closing =)
            logger::trace() << "client::do_stuff: rejecting " << inet_ntoa(tmp.sin_addr);
         }
      }
      if(tcp_sock_)
      {
         try
         {
            sync_data();
         }
         catch(tcp::net_error & e)
         {
            logger::warning() << "client::do_stuff: while syncing " << e.what();
            reset_tcp();
         }
      }
   }

   void get_users(std::vector<user_t> & res) const
   {
      lock_t __(users_mutex_);
      res.resize(users_.size());
      auto it = res.begin();
      for(auto const & u : users_)
      {
         *it = u.second;
         ++it;
      }
   }

private:
   udp::socket_t udp_sock_;
   tcp::socket_t tcp_server_sock_;
   boost::optional<tcp::socket_t> tcp_sock_;
   in_addr tcp_addr_;
   boost::optional<data_t> read_struct_, write_struct_;
   std::string host_;
   in_addr local_ip_;
   boost::optional<streamer_t> streamer_;

   std::string nick_;
   users_map_t users_;
   size_t stuff_hash_;
   mutable boost::recursive_mutex users_mutex_;

   int input_device_, output_device_, api_;
};

}
