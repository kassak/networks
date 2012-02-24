#pragma once

#include <stdexcept>

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace rline
{
#include <readline/readline.h>
#include <readline/history.h>
}
#include "tcp.hpp"

namespace pop3
{
   struct reset_error : std::runtime_error
   {
      reset_error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   struct client
   {
      client(std::string const & host, uint16_t port)
      {
         sock_.connect(host, port);
         std::cout << "Connected to " << host << " at " << port << std::endl;
      }

      void login(std::string const & user, std::string const & pass)
      {
         std::cout << "Login as " << user << "..." << std::endl;

         sock_ << "USER " << user << "\n";
         std::string msg;
         if(!handle_response(msg))
            throw reset_error("Login failed: " + msg);
         std::cout << msg << std::endl;
         sock_ << "PASS " << pass << "\n";
         if(!handle_response(msg))
            throw reset_error("Login failed: " + msg);
         std::cout << msg << std::endl;
      }

      bool handle_response(std::string& other)
      {
         std::string res = sock_.getline();
         if(boost::algorithm::starts_with(res, "+OK"))
         {
            if(res.length() > 4)
               other = res.substr(4);
            return true;
         }
         if(boost::algorithm::starts_with(res, "+ERR"))
         {
            if(res.length() > 5)
               other = res.substr(5);
            return false;
         }
         other = res;
         return false;
      }
   private:
      tcp::socket_t sock_;
   };

   struct tui
   {
      tui()
         : state_(ST_RESET)
      {
      }

      enum state_t
      {
         ST_SERVER,
         ST_LOGIN,
         ST_MENU,
         ST_EXIT,

         ST_RESET = ST_SERVER,
      };

      void handle_server()
      {
         char * tmp = rline::readline("pop3 server: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string srv(tmp);
         free(tmp);
         rline::add_history(srv.c_str());

         tmp = rline::readline("port: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string port_str(tmp);
         free(tmp);

         uint16_t port;
         try
         {
            port = boost::lexical_cast<uint16_t>(port_str);
         }
         catch(boost::bad_lexical_cast &)
         {
            throw reset_error("Port mustbe 0..65535");
         }

         client_ = boost::in_place(srv, port);
         state_ = ST_LOGIN;
      }

      void handle_login()
      {
         char * tmp = rline::readline("login: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string login(tmp);
         free(tmp);
         rline::add_history(login.c_str());

         tmp = getpass("password: ");

         client_->login(login, tmp);

         state_ = ST_MENU;
      }

      void run()
      {
         while(state_ != ST_EXIT)
         {
            try
            {
               switch(state_)
               {
               case ST_SERVER:
                  handle_server();
                  break;
               case ST_LOGIN:
                  handle_login();
                  break;
               default:
                  state_ = ST_EXIT;
               }
            }
            catch(reset_error & e)
            {
               std::cerr << e.what() << std::endl;
               state_ = ST_RESET;
            }
         }
      }
   private:
      boost::optional<client> client_;
      state_t state_;
   };
}
