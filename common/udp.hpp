#pragma once
#include "common/logger.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdexcept>
#include <string.h>
#include <assert.h>

#include <boost/optional.hpp>

namespace udp
{
   struct net_error : std::runtime_error
   {
      net_error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   struct socket_t
   {
      socket_t()
         : connected_(false)
      {
         sock_ = socket(PF_INET, SOCK_DGRAM, 0);
         if(sock_ == -1)
            throw std::runtime_error(std::string("Socket creation failure: ") + strerror(errno));
         bzero(&address_, sizeof(address_));
         int one = 1;
         setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
         setsockopt(sock_, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one));
      }

      int operator*() const
      {
         return sock_;
      }

      void connect(std::string const & host, uint16_t port)
      {
         hostent *hentity;

         hentity = gethostbyname(host.c_str());
         if(hentity == NULL)
            throw net_error(std::string("Get host by name failed: ") + strerror(errno));

         bcopy(hentity->h_addr, &address_.sin_addr, hentity->h_length);
         address_.sin_family = AF_INET;
         address_.sin_port = htons(port);
         connected_ = true;
      }

      void bind(boost::optional<uint16_t> const & port = boost::none)
      {
         sockaddr_in addr;
         bzero(&addr, sizeof(addr));
         addr.sin_family      = AF_INET;
         addr.sin_addr.s_addr = INADDR_ANY;
         addr.sin_port        = port ? *port : address_.sin_port;
         int res = ::bind(sock_, (sockaddr*)&addr, sizeof(addr));
         if(res == -1)
            throw net_error(std::string("Bind failed: ") + strerror(errno));
      }

      void set_echo(bool echo)
      {
         int flag = echo ? 1 : 0;
         int res = ::setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, &flag, sizeof(flag));
         if(res == -1)
            throw net_error(std::string("setsockopt(IP_MULTICAST_LOOP) failed: ") + strerror(errno));
      }

      void set_broadcast(bool broadcast)
      {
         int flag = broadcast ? 1 : 0;
         int res = ::setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag));
         if(res == -1)
            throw net_error(std::string("setsockopt(SO_BROADCAST) failed: ") + strerror(errno));
      }

      void join_group(bool join)
      {
         ip_mreq mreq;
         bzero(&mreq,sizeof(struct ip_mreq));
         bcopy(&address_.sin_addr, &mreq.imr_multiaddr.s_addr, sizeof(struct in_addr));
         // set interface
         mreq.imr_interface.s_addr = htonl(INADDR_ANY);

         // do membership call
         int res = setsockopt(sock_, IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, &mreq, sizeof(struct ip_mreq));
         if(res == -1)
            throw net_error(std::string("setsockopt(IP_ADD_MEMBERSHIP) failed: ") + strerror(errno));
      }

      template<class T>
      size_t sendto(in_addr const & addr, uint16_t port, const T * buffer, size_t size)
      {
         logger::trace() << "udpsock.sendto: " << inet_ntoa(addr);
         sockaddr_in saddr = {0};
         saddr.sin_family = AF_INET;
         saddr.sin_addr = addr;
         saddr.sin_port = htons(port);
         int res = ::sendto(sock_, reinterpret_cast<const char *>(buffer), sizeof(T)*size, 0, (sockaddr*)&saddr, sizeof(sockaddr_in));
         if(res == -1)
            throw net_error(std::string("sendto failed: ") + strerror(errno));

         assert((size_t)res == size*sizeof(T));
         return (size_t)res;
      }

      template<class T>
      size_t send(const T * buffer, size_t size)
      {
         return sendto(address_.sin_addr, htons(address_.sin_port), buffer, size);
      }

      template<class T>
      size_t recvfrom(in_addr & addr, uint16_t port, T * buffer, size_t size) //return in bytes!
      {
         sockaddr_in saddr = {0};
         saddr.sin_family = AF_INET;
         saddr.sin_addr = addr;
         saddr.sin_port = htons(port);
         socklen_t alen = sizeof(address_);
         int res = //::read(sock_, buffer, sizeof(T)*size);
         ::recvfrom(sock_, buffer, sizeof(T)*size, 0, (sockaddr*)&saddr, &alen);
         if(res == -1)
            throw net_error(std::string("recvfrom failed: ") + strerror(errno));
         logger::trace() << "udpsock.recvfrom: " << inet_ntoa(addr);
         if(alen)
            addr = saddr.sin_addr;
         return res;
      }

      template<class T>
      size_t recv(T * buffer, size_t size, in_addr * sender = NULL) //return in bytes!
      {
         in_addr tmp(address_.sin_addr);
         size_t res = recvfrom(tmp, ntohs(address_.sin_port), buffer, size);
         if(sender)
            *sender = tmp;
         return res;
      }

      ~socket_t()
      {
         if(connected_)
            join_group(false);
         ::close(sock_);
      }
   private:
      int sock_;
      sockaddr_in address_;
      bool connected_;
   };
}

