#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <boost/lexical_cast.hpp>

namespace tcp
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
      {
         sock_ = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
         if(sock_ == -1)
            throw std::runtime_error(std::string("Socket creation failure: ") + strerror(errno));
      }

      int operator*() const
      {
         return sock_;
      }

      void connect(std::string const & host, uint16_t port)
      {
         addrinfo hints;
         memset(&hints, 0, sizeof(hints));
         hints.ai_family = AF_INET;
         hints.ai_socktype = SOCK_STREAM;
         hints.ai_protocol = IPPROTO_TCP;

         addrinfo* addrs;
         int res = getaddrinfo(host.c_str(), NULL, &hints, &addrs);
         if(res != 0)
         {
//            freeaddrinfo(addrs);
            throw net_error(std::string("Get addr info failed: ") + gai_strerror(res));
         }
         if(addrs == NULL)
            throw net_error("No such address");

//         memcpy(&saddr, &addrs->ai_addr, sizeof(saddr));

         ((sockaddr_in*)addrs->ai_addr)->sin_port = htons(port);

         res = ::connect(sock_, addrs->ai_addr, addrs->ai_addrlen);
         freeaddrinfo(addrs);
         if(res == -1)
            throw net_error(std::string("Connection failed: ") + strerror(errno));

      }

      void bind(uint16_t port, const in_addr * addr = NULL)
      {
         sockaddr_in saddr = {0};
         saddr.sin_family = AF_INET;
         saddr.sin_port = htons(port);
         if(addr)
            saddr.sin_addr = *addr;
         int res = ::bind(sock_, (sockaddr*)&saddr, sizeof(saddr));
         if(res == -1)
            throw net_error(std::string("Bind failed: ") + strerror(errno));
      }

      void listen(size_t backlog = 10)
      {
         int res = ::listen(sock_, backlog);
         if(res == -1)
            throw net_error(std::string("Listen failed: ") + strerror(errno));
      }

      template<class T>
      socket_t & operator << (T const & x)
      {
         std::string str = boost::lexical_cast<std::string>(x);
         writeall(str.data(), str.size());
         return *this;
      }

      template<class T>
      size_t write(const T * data, size_t size, size_t offset)
      {
         int res = ::write(sock_, reinterpret_cast<const char *>(data) + offset, size);
         if(res == -1)
            throw net_error(std::string("Write failed: ") + strerror(errno));
         if(res == 0)
            throw net_error("Write failed: EOF");
         return res;
      }

      template<class T>
      void writeall(const T * data, size_t tsize)
      {
         size_t size = tsize*sizeof(T);
         size_t offset = 0;
         while(offset != size)
         {
            size_t res = write(sock_, data, size - offset);
            offset += res;
         }
      }

      template<class T>
      size_t read(T * data, size_t size, size_t offset = 0)
      {
         int res = ::read(sock_, reinterpret_cast<char*>(data) + offset, size);
         if(res == -1)
            throw net_error(std::string("Read failed: ") + strerror(errno));
         if(res == 0)
            throw net_error("Read failed: EOF");
         return res;
      }

      template<class T>
      void readall(T * data, size_t tsize)
      {
         size_t offset = 0;
         size_t size = tsize*sizeof(T);
         while(offset != size)
         {
            size_t res = read(data, size - offset, offset);
            offset += res;
//            std::cout << "~" << offset << std::endl;
         }
      }

      std::string getline()
      {
         std::stringstream out;
         while(true)
         {
            char c;
            read(&c, 1);
            if(c == '\n')
            {
               std::string res = out.str();
               if(res.back() == '\r')
                  return res.substr(0, res.length()-1);
               return res;
            }
            out << c;
         }
      }

      ~socket_t()
      {
         close(sock_);
      }
   private:
      int sock_;
   };
}
