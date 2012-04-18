#pragma once
#include "common/udp.hpp"
#include "common/tcp.hpp"
#include "common/logger.hpp"
#include "common/stuff.hpp"

#include <poll.h>
#include <unistd.h>
#include <boost/function.hpp>
#include <ifaddrs.h>
#include <unordered_map>

namespace s2m
{
   uint16_t SERVE_UDP_PORT = 12121;
   uint16_t SERVE_TCP_PORT = 12121;

struct client_t
{
   client_t(std::string const & host, boost::function<in_addr(std::vector<in_addr> const &)> const & ip_resolve)
      : host_(host)
      , nick_("defaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefaultdefault")
   {
      set_local_ip(ip_resolve);

      udp_sock_.connect(host, SERVE_UDP_PORT);
//      udp_sock_.set_broadcast(true);
      udp_sock_.join_group(true);
      udp_sock_.set_echo(true);
      udp_sock_.bind();

      tcp_server_sock_.bind(SERVE_TCP_PORT);
      tcp_server_sock_.listen();
   }

   void set_nick(std::string const & nick)
   {
      nick_ = nick;
      if(users_.empty())
         return;
      //TODO: update my nick? или нахуй?
   }

   void set_local_ip(boost::function<in_addr(std::vector<in_addr> const &)> const & resolve)
   {
      std::vector<in_addr> ips;
      ifaddrs* ifaddr = NULL;

      getifaddrs(&ifaddr);

      for(ifaddrs* it = ifaddr; it != NULL; it = it->ifa_next)
      {
         if(it->ifa_addr->sa_family==AF_INET)
         {
            in_addr tmp = ((sockaddr_in *)it->ifa_addr)->sin_addr;
            if(tmp.s_addr != (size_t)-1 && tmp.s_addr != 16777343)
               ips.push_back(tmp);
         }
      }
      if(ifaddr != NULL)
         ::freeifaddrs(ifaddr);
      if(ips.empty())
         std::runtime_error("No network ifaces found");
      if(ips.size() == 1)
         local_ip_ = ips.front();
      else
      {
         logger::trace() << "client::set_local_ip: Need resolving";
         local_ip_ = resolve(ips);
      }
      logger::trace() << "client::set_local_ip: ip = " << inet_ntoa(local_ip_);
   }

   void run()
   {
      users_.insert(std::make_pair(local_ip_, user_t(local_ip_, nick_)));
      stuff_hash_ = compute_hash();
      while(true)
      {
         do_stuff();
         sleep(1);
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
         if(res == 0)
            return false;
         validate(fd.revents, "read_non_block: ");
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
            sock.nodelay(true);
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
         read_struct_.reset();
         write_struct_.reset();
         tcp_sock_.reset();
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

      for(size_t i = 0; i < n; ++i)
         if(h[i].hash != stuff_hash_)
         {
            start_syncing(h[i].ip);
            break;
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
      auto it = std::back_inserter(data);
      for(auto const & user : users_)
      {
         it = std::copy(reinterpret_cast<const char*>(&user.second.ip), reinterpret_cast<const char*>(&user.second.ip) + sizeof(user.second.ip), it);
         *it = user.second.nick.size();
         ++it;
         it = std::copy(user.second.nick.begin(), user.second.nick.end(), it);
      }
   }

   uint32_t compute_hash() const
   {
      uint32_t res = 0;
      for(auto const & user : users_)
      {
         util::hash_combine(res, util::hash(user.second.ip));
         logger::trace() << res;
         util::hash_combine(res, util::hash(user.second.nick));
         logger::trace() << res;
      }
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
      if(fds[0].revents & POLLOUT)
         sendhash();
      if(fds[0].revents & POLLIN)
         recvhash();

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
            tcp::socket_t(res);//it is really closing =)
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

   struct user_t
   {
      user_t(in_addr const & ip, std::string const& nick)
         : ip(ip)
         , nick(nick)
      {
      }

      in_addr ip;
      std::string nick;
      size_t timestamp;
   };

   typedef
      std::unordered_map<in_addr, user_t, util::hasher<in_addr>>
      users_map_t;
private:
   udp::socket_t udp_sock_;
   tcp::socket_t tcp_server_sock_;
   boost::optional<tcp::socket_t> tcp_sock_;
   in_addr tcp_addr_;
   boost::optional<data_t> read_struct_, write_struct_;
   std::string host_;
   in_addr local_ip_;

   std::string nick_;
   users_map_t users_;
   size_t stuff_hash_;
};

}
