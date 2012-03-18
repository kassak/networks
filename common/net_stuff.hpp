#pragma once
#include <poll.h>

namespace util
{
   template<class E>
   size_t poll(pollfd * fds, size_t cnt, size_t timeout)
   {
      int res = ::poll(fds, cnt, timeout);
      if(res == -1)
         throw E(std::string("Poll failed: ") + strerror(errno));
      return (size_t)res;
   }
}
