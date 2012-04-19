#pragma once
#include <boost/functional/hash.hpp>
#include <ifaddrs.h>

bool operator < (in_addr const & a, in_addr const & b)
{
   return *reinterpret_cast<const uint32_t*>(&a) < *reinterpret_cast<const uint32_t*>(&b);
}
/*
bool operator == (in_addr const & a, in_addr const & b)
{
   return *reinterpret_cast<const uint32_t*>(&a) == *reinterpret_cast<const uint32_t*>(&b);
}*/
namespace util
{
   in_addr resolve(std::vector<in_addr> const & ips)
   {
      std::cout << "Multiple interfaces found. Select right." << std::endl;
      for(size_t i = 0; i < ips.size(); ++i)
      {
         std::cout << i << " " << inet_ntoa(ips[i]) << std::endl;
      }
      std::cout << "Number: ";
      std::cout.flush();
      size_t idx;
      std::cin >> idx;
      return ips[idx];
   }


   template<class Foo>
   in_addr get_local_ip(Foo const & resolve)
   {
      std::vector<in_addr> ips;
      ifaddrs* ifaddr = NULL;
      in_addr res;

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
         res = ips.front();
      else
      {
         logger::trace() << "get_local_ip: Need resolving";
         res = resolve(ips);
      }
      logger::trace() << "get_local_ip: ip = " << inet_ntoa(res);
      return res;
   }


   template<class T>
   void nullize(T & mem)
   {
      ::bzero(&mem, sizeof(T));
   }


   template<class T>
   uint32_t hash(T const & x)
   {
      return x;
   }

   void hash_combine(uint32_t & h1, uint32_t h2)
   {
      h1 = ((h1<<1) + 239)^h2;
   }

   template<class Iterator>
   uint32_t hash_range(Iterator b, Iterator e)
   {
      uint32_t res = 0;
      for(; b != e; ++b)
         hash_combine(res, hash(*b));
      return res;
   }

   uint32_t hash(const in_addr &x )
   {
      return hash_range(reinterpret_cast<const char*>(&x), reinterpret_cast<const char*>(&x) + sizeof(in_addr));
   }

   uint32_t hash(std::string const & str)
   {
      return hash_range(str.begin(), str.end());
   }

   template<class T>
   struct hasher
   {
      uint32_t operator()(T const & t) const
      {
         return hash(t);
      }
   };

/*
   template<class T, class... V>
   struct max_type_f
   {
      typedef typename max_type_f<T, typename max_type_f<V...>::type>::type type;
   };

   template<class T, class U>
   struct max_type_f<T, U>
   {
      typedef decltype(*(T*)0 + *(U*)0) type;
   };
*/
   template<class T>
   T sqr(T const & x)
   {
      return x*x;
   }

   template<class T, class U>
   auto max(T const & a, U const & b) -> decltype(a+b)
   {
      return (a < b) ? b : a ;
   }

   template<class T, class U>
   auto min(T const & a, U const & b) -> decltype(a+b)
   {
      return (a < b) ? a : b ;
   }

   template<class T, class U, class V>
   auto bound(T const & x, U const & mi, V const & ma) -> decltype(x+mi+ma)
   {
      return max(min(x, ma), mi);
   }

   template<class T>
   double energy(const T * s, size_t n)
   {
      double res = 0;
      for(size_t i = 0; i < n; ++i)
         res += sqr(s[i]/(double)std::numeric_limits<T>::max());
      res /= n;
      return res;
   }
}

bool operator == (in_addr const & a, in_addr const & b)
{
   return std::equal(reinterpret_cast<const char*>(&a),
                     reinterpret_cast<const char*>(&a) + sizeof(in_addr),
                     reinterpret_cast<const char*>(&b));
}
