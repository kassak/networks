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
#include "common/tcp.hpp"
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

namespace smtp
{
   struct reset_error : std::runtime_error
   {
      reset_error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   struct smtp_error : std::runtime_error
   {
      smtp_error(std::string const & what)
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
         std::string msg;
         size_t code;
         code = handle_response(msg);
         if(code != 220)
            throw smtp_error("Connect failed: " + msg);
      }

      void login(std::string const & chost, std::string const & user, std::string const & pass)
      {
         sock_ << "HELO " << chost << "\n";
         std::string msg;
         size_t code;
         code = handle_response(msg);
         if(code != 250)
            throw smtp_error("Login failed: " + msg);

         sock_ << "AUTH LOGIN\n";
         code = handle_response(msg);
         if(code != 334)
            throw smtp_error("Login failed: " + msg);

         using namespace boost::archive::iterators;

         typedef
             base64_from_binary<
                 transform_width<std::string::const_iterator, 6, 8>
         > base64_t;

         typedef
             transform_width<
                 binary_from_base64<std::string::const_iterator>, 8, 6
         > binary_t;

         std::string
            b64_user(base64_t(user.begin()), base64_t(user.end())),
            b64_pass(base64_t(pass.begin()), base64_t(pass.end()));

         std::cout << "!" << b64_user << std::endl;

         sock_ << b64_user << "\n";
         code = handle_response(msg);
         if(code != 334)
            throw smtp_error("Login failed: " + msg);
         sock_ << b64_pass << "\n";
         code = handle_response(msg);
         if(code != 235)
            throw smtp_error("Login failed: " + msg);
      }

      size_t handle_response(std::string& other)
      {
         std::string resp = sock_.getline();
         std::cout << "**" <<resp <<std::endl;
         size_t res;
         std::stringstream ss(resp);
         ss >> res;
         std::getline(ss, other);
         return res;
      }

      void quit()
      {
         sock_ << "QUIT\n";
         std::string tmp;
         handle_response(tmp);
      }

      size_t stat()
      {
         sock_ << "STAT\n";
         std::string msg;
         if(!handle_response(msg))
            throw smtp_error("Error while STAT: " + msg);
         std::stringstream ss;
         ss << msg;
         size_t res;
         ss >> res;
         return res;
      }

      void reset()
      {
         sock_ << "RSET\n";
         std::string msg;
         if(!handle_response(msg))
            throw smtp_error("Error while RSET: " + msg);
      }

      size_t msg_size(size_t id)
      {
         sock_ << "LIST " << id << "\n";
         std::string msg;
         if(!handle_response(msg))
            throw smtp_error("Error while LIST: " + msg);
         std::stringstream ss;
         ss << msg;
         size_t res;
         ss >> res;
         return res;
      }

      void remove(size_t id)
      {
         sock_ << "DELE " << id << "\n";
         std::string msg;
         if(!handle_response(msg))
            throw smtp_error("Error while DELE: " + msg);
      }

      void recieve(size_t id, std::vector<char> & body)
      {
         sock_ << "RETR " << id << "\n";
         std::string msg;
         if(!handle_response(msg))
            throw smtp_error("Error while RETR: " + msg);
         std::stringstream ss;
         ss << msg;
         size_t size;
         ss >> size;
         body.resize(size);
         sock_.read(&body[0], size);
      }

   private:
      tcp::socket_t sock_;
   };

   struct tui
   {
      tui()
         : state_(ST_RESET)
      {
         rline::read_history(".srv_hist");
      }

      ~tui()
      {
         rline::write_history(".srv_hist");
      }

      enum state_t
      {
         ST_SERVER,
         ST_LOGIN,
         ST_MENU,
         ST_EXIT,

         ST_RESET = ST_SERVER,
      };

      void handle_menu()
      {
         char c;
         do
         {
            try
            {
               std::cout << "\nYou can: " << std::endl
               << "q - Exit"              << std::endl
               << "c - Count messages"    << std::endl
               << "r - Retrieve message"  << std::endl
               << "x - Reset"             << std::endl
               << "d - Delete"            << std::endl
               ;
               c = getchar();
               std::cout << std::endl;
               switch(c)
               {
               case 'r':
                  {
                     char* tmp = rline::readline("Message id: ");
                     if(tmp == NULL)
                        throw reset_error("EOF");
                     std::string ss(tmp);
                     free(tmp);
                     size_t id;
                     try
                     {
                        id = boost::lexical_cast<size_t>(ss);
                     }
                     catch(boost::bad_lexical_cast&)
                     {
                        std::cerr << "bad id" << std::endl;
                        continue;
                     }
                     std::vector<char> body;
                     client_->recieve(id, body);
                     std::cout.write(&body[0], body.size());
                  }
                  break;
               case 'd':
                  {
                     char* tmp = rline::readline("Message id: ");
                     if(tmp == NULL)
                        throw reset_error("EOF");
                     std::string ss(tmp);
                     free(tmp);
                     size_t id;
                     try
                     {
                        id = boost::lexical_cast<size_t>(ss);
                     }
                     catch(boost::bad_lexical_cast&)
                     {
                        std::cerr << "bad id" << std::endl;
                        continue;
                     }
                     client_->remove(id);
                  }
                  break;
               case 'x':
                  client_->reset();
                  break;
               case 'c':
                  std::cout << client_->stat() << " - messages" << std::endl;
                  break;
               }
            }
            catch(smtp_error& e)
            {
               std::cerr << "Pop error: " << e.what() << std::endl;
            }
         }
         while(c != 'q');
         client_->quit();
      }

      void handle_server()
      {
         char * tmp = rline::readline("smtp server: ");
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
      }

      void handle_login()
      {
         char * tmp = rline::readline("FQDN: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string fqdn(tmp);
         free(tmp);
         rline::add_history(fqdn.c_str());

         tmp = rline::readline("login: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string login(tmp);
         free(tmp);
         rline::add_history(login.c_str());

         tmp = getpass("password: ");

         client_->login(fqdn, login, tmp);
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
                  state_ = ST_LOGIN;
                 break;
               case ST_LOGIN:
                  handle_login();
                  state_ = ST_MENU;
                  break;
               case ST_MENU:
                  handle_menu();
                  state_ = ST_RESET;
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
            catch(smtp_error & e)
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
