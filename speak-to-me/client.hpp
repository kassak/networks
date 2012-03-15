#pragma once
#include "common/udp.hpp"
#include <poll.h>
#include <unistd.h>

namespace s2m
{
   uint16_t SERVE_UDP_SOCKET = 12121;

struct client_t
{
   client_t(std::string const & host)
      : host_(host)
   {
      udp_sock_.connect(host, SERVE_UDP_SOCKET);
      udp_sock_.bind(SERVE_UDP_SOCKET);
   }

   void run()
   {
      while(true)
      {
         exchange_hashes();
         sleep(1);
      }
   }

   struct hash_struct
   {
      int hash;
   };

   void sendhash()
   {
      hash_struct h;
      h.hash = 0;

      in_addr addr;
      addr.s_addr = INADDR_ANY;
      udp_sock_.sendto(addr, htons(SERVE_UDP_SOCKET), &h, 1);
      std::cout << "Hash sended: " << h.hash << std::endl;
   }

   void recvhash()
   {
      hash_struct h[10];

      in_addr addr;
      addr.s_addr = INADDR_ANY;
      size_t n = udp_sock_.recvfrom(addr, htons(SERVE_UDP_SOCKET), h, 10);
      assert(n % sizeof(hash_struct) == 0);
      n /= sizeof(hash_struct);
      std::cout << "Hash recieved(" << n << "): " << h[0].hash << std::endl;
   }

   void exchange_hashes()
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
   std::string host_;
};

}
