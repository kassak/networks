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
