#pragma once

#include <memory>
#include <iostream>
#include <sstream>

#include <assert.h>

namespace logger
{
   enum log_level_t
   {
      ERROR = 0,
      WARNING,
      DEBUG,
      TRACE,

      LL_COUNT
   };

   namespace details
   {
      struct level_printer
      {
         level_printer(log_level_t lvl) : lvl_(lvl){}

         std::string operator()() const
         {
            switch(lvl_)
            {
            #define E(X) case X: return #X
            E(ERROR);
            E(WARNING);
            E(DEBUG);
            E(TRACE);
            #undef E
            default:
               return "unknown";
            }
         }
      private:
         log_level_t lvl_;
      };

      struct i_stream_holder
      {
         virtual void write(std::string const&) = 0;
      };

      struct null_stream_holder : i_stream_holder
      {
         void write(std::string const&){};
      };

      template<class Foo, class Stream>
      struct stream_holder_t : i_stream_holder
      {
         stream_holder_t(Foo const & prefix, Stream & stream)
            : stream_(stream)
            , prefix_(prefix)
         {
         }

         void write(std::string const& str)
         {
            stream_ << prefix_() << " " << str << "\n";
            stream_.flush();
         }
      private:
         Stream stream_;
         Foo prefix_;
      };

      struct stream_writer_t
      {
         stream_writer_t(i_stream_holder * sh)
            : stream_(sh)
         {
         }

         stream_writer_t(stream_writer_t const & other)
         {
            stream_ = other.stream_;
         }

         ~stream_writer_t()
         {
            std::string str(ss_.str());
            if(!str.empty())
               stream_->write(str);
         }

         template<class T>
         stream_writer_t & operator << (T const & x)
         {
            ss_ << x;
            return *this;
         }
      private:
         std::stringstream ss_;
         i_stream_holder * stream_;
      };

   }
   typedef
      std::shared_ptr<details::i_stream_holder>
      stream_holder_ptr;

   template<class F, class S>
   stream_holder_ptr holder_by_val(F const & prefix, S const & s)
   {
      return stream_holder_ptr(new details::stream_holder_t<F, S>(prefix, s));
   };

   template<class F, class S>
   stream_holder_ptr holder_by_ref(F const & prefix, S & s)
   {
      return stream_holder_ptr(new details::stream_holder_t<F, S&>(prefix, s));
   };

   stream_holder_ptr null_holder()
   {
      return stream_holder_ptr(new details::null_stream_holder());
   }

   //NOTE: be careful! don't loose ownership of returned pointer =)
   inline stream_holder_ptr & holder(log_level_t lvl)
   {
      static stream_holder_ptr logs[LL_COUNT] = {
         holder_by_ref(details::level_printer(ERROR),   std::cerr),
         holder_by_ref(details::level_printer(WARNING), std::cerr),
         holder_by_ref(details::level_printer(DEBUG),   std::cout),
         holder_by_ref(details::level_printer(TRACE),   std::cout),
      };
      assert(lvl < LL_COUNT);
      return logs[lvl];
   }

   inline details::stream_writer_t logger(log_level_t lvl)
   {
      return details::stream_writer_t(holder(lvl).get());
   }

   inline void set_logger(log_level_t lvl, stream_holder_ptr hld)
   {
      holder(lvl) = hld;
   }

   inline details::stream_writer_t error()
   {
      return logger(ERROR);
   }

   inline details::stream_writer_t warning()
   {
      return logger(WARNING);
   }

   inline details::stream_writer_t debug()
   {
      return logger(DEBUG);
   }

   inline details::stream_writer_t trace()
   {
      return logger(TRACE);
   }

}
