#pragma once
#include <boost/functional/hash.hpp>

namespace util
{
   struct hash_in_addr
   {
      size_t operator()(const in_addr &x ) const
      {
         return boost::hash_range(reinterpret_cast<const char*>(&x), reinterpret_cast<const char*>(&x) + sizeof(in_addr));
      }
   };

   template<class T>
   T sqr(T const & x)
   {
      return x*x;
   }
}

bool operator == (in_addr const & a, in_addr const & b)
{
   return std::equal(reinterpret_cast<const char*>(&a),
                     reinterpret_cast<const char*>(&a) + sizeof(in_addr),
                     reinterpret_cast<const char*>(&b));
}
